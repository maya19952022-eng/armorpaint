#include "../global.h"

// Bounding volume hierarchy over world space triangles, for rays cast on the cpu by the bakers.
// Built once per bake, read only afterwards so worker threads can share it.

#define UTIL_BVH_LEAF_SIZE 4
#define UTIL_BVH_STACK     64

static f32 util_bvh_centroid(bvh_t *b, i32 tri, i32 axis) {
	f32 *t = b->tris + tri * 9;
	return t[axis] + t[3 + axis] + t[6 + axis];
}

static void util_bvh_swap(bvh_t *b, i32 i, i32 j) {
	f32 tmp[9];
	memcpy(tmp, b->tris + i * 9, sizeof(tmp));
	memcpy(b->tris + i * 9, b->tris + j * 9, sizeof(tmp));
	memcpy(b->tris + j * 9, tmp, sizeof(tmp));
	i32 o         = b->objects[i];
	b->objects[i] = b->objects[j];
	b->objects[j] = o;
}

// Partial sort of lo..hi by centroid: position k gets its sorted triangle, smaller ones end up before it
static void util_bvh_select(bvh_t *b, i32 lo, i32 hi, i32 k, i32 axis) {
	while (hi > lo) {
		f32 pivot = util_bvh_centroid(b, (lo + hi) / 2, axis);
		i32 i     = lo;
		i32 j     = hi;
		while (i <= j) {
			while (util_bvh_centroid(b, i, axis) < pivot) {
				i++;
			}
			while (util_bvh_centroid(b, j, axis) > pivot) {
				j--;
			}
			if (i <= j) {
				util_bvh_swap(b, i, j);
				i++;
				j--;
			}
		}
		if (k <= j) {
			hi = j;
		}
		else if (k >= i) {
			lo = i;
		}
		else {
			return;
		}
	}
}

static void util_bvh_bounds(bvh_t *b, bvh_node_t *n) {
	for (i32 a = 0; a < 3; ++a) {
		n->min[a] = FLT_MAX;
		n->max[a] = -FLT_MAX;
	}
	for (i32 i = n->first; i < n->first + n->count; ++i) {
		f32 *t = b->tris + i * 9;
		for (i32 v = 0; v < 3; ++v) {
			for (i32 a = 0; a < 3; ++a) {
				f32 x     = t[v * 3 + a];
				n->min[a] = x < n->min[a] ? x : n->min[a];
				n->max[a] = x > n->max[a] ? x : n->max[a];
			}
		}
	}
}

static void util_bvh_split(bvh_t *b, i32 node) {
	bvh_node_t *n = &b->nodes[node];
	util_bvh_bounds(b, n);
	if (n->count <= UTIL_BVH_LEAF_SIZE) {
		return;
	}
	// Median split along the longest axis
	f32 ex[3] = {n->max[0] - n->min[0], n->max[1] - n->min[1], n->max[2] - n->min[2]};
	i32 axis  = ex[0] > ex[1] ? (ex[0] > ex[2] ? 0 : 2) : (ex[1] > ex[2] ? 1 : 2);
	i32 first = n->first;
	i32 count = n->count;
	i32 half  = count / 2;
	util_bvh_select(b, first, first + count - 1, first + half, axis);

	i32 left = b->node_count;
	b->node_count += 2;
	b->nodes[left].first     = first;
	b->nodes[left].count     = half;
	b->nodes[left + 1].first = first + half;
	b->nodes[left + 1].count = count - half;
	b->nodes[node].first     = left;
	b->nodes[node].count     = 0;
	util_bvh_split(b, left);
	util_bvh_split(b, left + 1);
}

// Takes ownership of tris (9 floats per triangle) and objects
void util_bvh_build(bvh_t *b, f32 *tris, i32 *objects, i32 tri_count) {
	b->tris       = tris;
	b->objects    = objects;
	b->tri_count  = tri_count;
	b->nodes      = malloc(sizeof(bvh_node_t) * (tri_count > 0 ? tri_count * 2 : 1));
	b->node_count = 1;
	b->nodes[0]   = (bvh_node_t){.first = 0, .count = tri_count};
	if (tri_count > 0) {
		util_bvh_split(b, 0);
	}
	else {
		for (i32 a = 0; a < 3; ++a) {
			b->nodes[0].min[a] = 0.0;
			b->nodes[0].max[a] = 0.0;
		}
	}
}

void util_bvh_free(bvh_t *b) {
	free(b->tris);
	free(b->objects);
	free(b->nodes);
	*b = (bvh_t){0};
}

static bool util_bvh_ray_box(bvh_node_t *n, f32 *o, f32 *inv, f32 tmax) {
	f32 t0 = 0.0;
	f32 t1 = tmax;
	for (i32 a = 0; a < 3; ++a) {
		f32 ta = (n->min[a] - o[a]) * inv[a];
		f32 tb = (n->max[a] - o[a]) * inv[a];
		if (ta > tb) {
			f32 tmp = ta;
			ta      = tb;
			tb      = tmp;
		}
		t0 = ta > t0 ? ta : t0;
		t1 = tb < t1 ? tb : t1;
		if (t0 > t1) {
			return false;
		}
	}
	return true;
}

// Moller-Trumbore, both faces
static bool util_bvh_ray_tri(f32 *t, f32 *o, f32 *d, f32 tmax, bvh_hit_t *hit) {
	f32 e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
	f32 e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
	f32 p[3]  = {d[1] * e2[2] - d[2] * e2[1], d[2] * e2[0] - d[0] * e2[2], d[0] * e2[1] - d[1] * e2[0]};
	f32 det   = e1[0] * p[0] + e1[1] * p[1] + e1[2] * p[2];
	if (fabsf(det) < 1e-12f) {
		return false;
	}
	f32 inv  = 1.0f / det;
	f32 s[3] = {o[0] - t[0], o[1] - t[1], o[2] - t[2]};
	f32 u    = (s[0] * p[0] + s[1] * p[1] + s[2] * p[2]) * inv;
	if (u < 0.0f || u > 1.0f) {
		return false;
	}
	f32 q[3] = {s[1] * e1[2] - s[2] * e1[1], s[2] * e1[0] - s[0] * e1[2], s[0] * e1[1] - s[1] * e1[0]};
	f32 v    = (d[0] * q[0] + d[1] * q[1] + d[2] * q[2]) * inv;
	if (v < 0.0f || u + v > 1.0f) {
		return false;
	}
	f32 dist = (e2[0] * q[0] + e2[1] * q[1] + e2[2] * q[2]) * inv;
	if (dist <= 0.0f || dist >= tmax) {
		return false;
	}
	hit->t = dist;
	hit->u = u;
	hit->v = v;
	return true;
}

// Closest hit closer than tmax, or any hit when any is set. Returns false on a miss.
bool util_bvh_raycast(bvh_t *b, f32 *o, f32 *d, f32 tmax, bool any, bvh_hit_t *hit) {
	if (b->tri_count == 0) {
		return false;
	}
	f32 inv[3];
	for (i32 a = 0; a < 3; ++a) {
		inv[a] = 1.0f / (fabsf(d[a]) > 1e-12f ? d[a] : (d[a] < 0.0f ? -1e-12f : 1e-12f));
	}
	i32  stack[UTIL_BVH_STACK];
	i32  top     = 0;
	bool found   = false;
	hit->t       = tmax;
	hit->tri     = -1;
	stack[top++] = 0;
	while (top > 0) {
		bvh_node_t *n = &b->nodes[stack[--top]];
		if (!util_bvh_ray_box(n, o, inv, hit->t)) {
			continue;
		}
		if (n->count > 0) {
			for (i32 i = n->first; i < n->first + n->count; ++i) {
				bvh_hit_t h;
				if (util_bvh_ray_tri(b->tris + i * 9, o, d, hit->t, &h)) {
					*hit     = h;
					hit->tri = i;
					found    = true;
					if (any) {
						return true;
					}
				}
			}
		}
		else if (top + 2 <= UTIL_BVH_STACK) {
			stack[top++] = n->first;
			stack[top++] = n->first + 1;
		}
	}
	return found;
}
