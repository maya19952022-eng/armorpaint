#include "../global.h"

// Mesh maps: textures baked from the geometry of the project, one per mesh_map_slot_t, read by the Mesh Map node.
// Baked on the cpu in uv space by a queue of jobs that run a slice of rows each frame.
// See base/docs/features/mesh_maps.md.

typedef struct util_bake_raster {
	i32   res;
	f32  *pos;    // 3 floats per texel, world space
	f32  *nor;    // 3 floats per texel, world space, unit length
	i32  *object; // Paint object index per texel, -1 outside the uv islands
	u8   *color;  // 4 bytes per texel, vertex colors, NULL when not needed
	f32   min[3]; // Bounds of the rasterized geometry
	f32   max[3];
	f32   size; // Diagonal of the bounds
	bvh_t bvh;  // All paint objects, built for the slots that cast rays
} util_bake_raster_t;

struct bake_job {
	i32                 slot; // mesh_map_slot_t, -1 for a custom job
	bake_settings_t     settings;
	util_bake_raster_t *raster;
	f32                *out; // 4 floats per texel
	volatile i32        next_row;
	i32                 rows_done;
	i32                 rows_per_frame;
	f32                 progress;
	bool                cancelled;
	bool (*step)(struct bake_job *job); // Custom job, called every frame until it returns true
	void (*cancel)(struct bake_job *job);
	void (*done)(void *data);
	void *done_data;
	void *data;
	void *owner; // Custom job, the node or tool that started it
};

// clang-format off
#define UTIL_BAKE_DEFAULT_SETTINGS {    \
	.res                = 2048,         \
	.dilation           = 16,           \
	.occlusion_samples  = 64,           \
	.occlusion_distance = 0.25,         \
	.thickness_samples  = 32,           \
	.thickness_distance = 0.5,          \
	.curvature_strength = 1.0,          \
	.id_source          = MESH_MAP_ID_OBJECT, \
	.maps               = (1 << MESH_MAP_COUNT) - 1 \
}
// clang-format on

bake_settings_t util_bake_settings = UTIL_BAKE_DEFAULT_SETTINGS;

static gpu_texture_t *util_bake_maps[MESH_MAP_COUNT];
static gpu_texture_t *util_bake_defaults[MESH_MAP_COUNT];
static bake_job_t   **util_bake_jobs       = NULL;
static i32            util_bake_jobs_count = 0;
static bake_job_t    *util_bake_job        = NULL;

#define UTIL_BAKE_THREADS      16
#define UTIL_BAKE_FRAME_BUDGET 0.03 // Seconds of work per frame

char *util_bake_slot_name(i32 slot) {
	switch (slot) {
	case MESH_MAP_NORMAL:
		return tr("Normal");
	case MESH_MAP_WORLD_NORMAL:
		return tr("World Space Normal");
	case MESH_MAP_ID:
		return tr("ID");
	case MESH_MAP_OCCLUSION:
		return tr("Ambient Occlusion");
	case MESH_MAP_CURVATURE:
		return tr("Curvature");
	case MESH_MAP_POSITION:
		return tr("Position");
	case MESH_MAP_THICKNESS:
		return tr("Thickness");
	case MESH_MAP_BENT_NORMAL:
		return tr("Bent Normals");
	case MESH_MAP_OPACITY:
		return tr("Opacity");
	}
	return "";
}

string_array_t *util_bake_slot_names() {
	string_array_t *names = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < MESH_MAP_COUNT; ++i) {
		any_array_push(names, util_bake_slot_name(i));
	}
	return names;
}

// Directions and positions keep 16 bits per channel
bool util_bake_slot_is_vector(i32 slot) {
	return slot == MESH_MAP_NORMAL || slot == MESH_MAP_WORLD_NORMAL || slot == MESH_MAP_POSITION || slot == MESH_MAP_BENT_NORMAL;
}

// Value of a slot that is not baked, and of the texels outside the uv islands
static void util_bake_default_value(i32 slot, f32 *v) {
	v[0] = v[1] = v[2] = 0.0;
	v[3]               = 1.0;
	if (slot == MESH_MAP_NORMAL) {
		v[0] = v[1] = 0.5;
		v[2]        = 1.0;
	}
	else if (slot == MESH_MAP_OCCLUSION) {
		v[0] = v[1] = v[2] = 1.0;
	}
	else if (slot == MESH_MAP_CURVATURE) {
		v[0] = v[1] = v[2] = 0.5;
	}
}

gpu_texture_t *util_bake_mesh_map(i32 slot) {
	return slot >= 0 && slot < MESH_MAP_COUNT ? util_bake_maps[slot] : NULL;
}

// Texture sampled by the Mesh Map node, a 1x1 default until the slot is baked
gpu_texture_t *util_bake_texture(i32 slot) {
	if (slot < 0 || slot >= MESH_MAP_COUNT) {
		slot = MESH_MAP_OPACITY; // Black
	}
	if (util_bake_maps[slot] != NULL) {
		return util_bake_maps[slot];
	}
	if (util_bake_defaults[slot] == NULL) {
		f32 v[4];
		util_bake_default_value(slot, v);
		buffer_t *b = buffer_create(4);
		for (i32 i = 0; i < 4; ++i) {
			b->buffer[i] = (u8)(v[i] * 255.0 + 0.5);
		}
		util_bake_defaults[slot] = gpu_create_texture_from_bytes_raw(b, 1, 1, GPU_TEXTURE_FORMAT_RGBA32);
	}
	return util_bake_defaults[slot];
}

static f32 util_bake_hash(u32 x) {
	x ^= x >> 16;
	x *= 0x7feb352dU;
	x ^= x >> 15;
	x *= 0x846ca68bU;
	x ^= x >> 16;
	return (x >> 8) * (1.0f / 16777216.0f);
}

static f32 util_bake_radical_inverse(u32 i) {
	i = (i << 16) | (i >> 16);
	i = ((i & 0x55555555U) << 1) | ((i & 0xAAAAAAAAU) >> 1);
	i = ((i & 0x33333333U) << 2) | ((i & 0xCCCCCCCCU) >> 2);
	i = ((i & 0x0F0F0F0FU) << 4) | ((i & 0xF0F0F0F0U) >> 4);
	i = ((i & 0x00FF00FFU) << 8) | ((i & 0xFF00FF00U) >> 8);
	return i * (1.0f / 4294967296.0f);
}

// Cosine weighted direction around n, sample i of count, rotated per texel so the noise does not band
static void util_bake_hemisphere(f32 *n, i32 i, i32 count, f32 r1, f32 r2, f32 *d) {
	f32 u1 = (i + r1) / count;
	f32 u2 = util_bake_radical_inverse(i) + r2;
	u2 -= floorf(u2);
	f32 r   = sqrtf(u1);
	f32 phi = 2.0f * (f32)IRON_PI * u2;
	f32 x   = r * cosf(phi);
	f32 y   = r * sinf(phi);
	f32 z   = sqrtf(1.0f - u1 > 0.0f ? 1.0f - u1 : 0.0f);
	// Orthonormal basis, Duff et al. 2017
	f32 sign = n[2] >= 0.0f ? 1.0f : -1.0f;
	f32 a    = -1.0f / (sign + n[2]);
	f32 b    = n[0] * n[1] * a;
	f32 t[3] = {1.0f + sign * n[0] * n[0] * a, sign * b, -sign * n[0]};
	f32 s[3] = {b, sign + n[1] * n[1] * a, -n[1]};
	for (i32 k = 0; k < 3; ++k) {
		d[k] = t[k] * x + s[k] * y + n[k] * z;
	}
}

static vec4_t util_bake_vertex(i16 *a, i32 i, i32 stride, i32 count) {
	i16 *p = a + i * stride;
	return (vec4_t){p[0] / 32767.0f, p[1] / 32767.0f, count > 2 ? p[2] / 32767.0f : 0.0f, 0.0f};
}

static void util_bake_raster_free(util_bake_raster_t *r) {
	if (r == NULL) {
		return;
	}
	free(r->pos);
	free(r->nor);
	free(r->object);
	free(r->color);
	util_bvh_free(&r->bvh);
	free(r);
}

// World positions and normals of all paint objects in their uv space, one sample at each texel center
static util_bake_raster_t *util_bake_raster_create(i32 res, bool colors, bool rays) {
	util_bake_raster_t *r = calloc(1, sizeof(util_bake_raster_t));
	r->res                = res;
	r->pos                = calloc((size_t)res * res * 3, sizeof(f32));
	r->nor                = calloc((size_t)res * res * 3, sizeof(f32));
	r->object             = malloc((size_t)res * res * sizeof(i32));
	r->color              = colors ? calloc((size_t)res * res * 4, 1) : NULL;
	for (i32 i = 0; i < res * res; ++i) {
		r->object[i] = -1;
	}
	for (i32 a = 0; a < 3; ++a) {
		r->min[a] = FLT_MAX;
		r->max[a] = -FLT_MAX;
	}

	mesh_object_t_array_t *objects   = g_project->_->paint_objects;
	i32                    tri_count = 0;
	for (i32 oi = 0; oi < objects->length; ++oi) {
		tri_count += objects->buffer[oi]->data->index_array->length / 3;
	}
	f32 *tris     = rays ? malloc(sizeof(f32) * 9 * (tri_count > 0 ? tri_count : 1)) : NULL;
	i32 *tri_objs = rays ? malloc(sizeof(i32) * (tri_count > 0 ? tri_count : 1)) : NULL;
	i32  tri_i    = 0;

	for (i32 oi = 0; oi < objects->length; ++oi) {
		mesh_object_t  *o    = objects->buffer[oi];
		mesh_data_t    *md   = o->data;
		vertex_array_t *vpos = mesh_data_get_vertex_array(md, "pos");
		vertex_array_t *vnor = mesh_data_get_vertex_array(md, "nor");
		vertex_array_t *vtex = mesh_data_get_vertex_array(md, "tex");
		vertex_array_t *vcol = colors ? mesh_data_get_vertex_array(md, "col") : NULL;
		if (vpos == NULL || vnor == NULL) {
			continue;
		}
		i16         *pa  = vpos->values->buffer;
		i16         *na  = vnor->values->buffer;
		i16         *ta  = vtex != NULL ? vtex->values->buffer : NULL;
		i16         *ca  = vcol != NULL ? vcol->values->buffer : NULL;
		u32_array_t *ind = md->index_array;
		mat4_t       m   = o->base->transform->world_unpack;
		mat4_t       mn  = o->base->transform->world;

		for (i32 t = 0; t + 2 < ind->length; t += 3) {
			vec4_t p[3];
			vec4_t n[3];
			f32    x[3];
			f32    y[3];
			for (i32 k = 0; k < 3; ++k) {
				i32    i  = ind->buffer[t + k];
				vec4_t v  = util_bake_vertex(pa, i, 4, 3);
				v.w       = 1.0;
				p[k]      = vec4_apply_mat4(v, m);
				vec4_t w  = util_bake_vertex(na, i, 2, 2);
				w.z       = pa[i * 4 + 3] / 32767.0f;
				w.w       = 0.0;
				n[k]      = vec4_apply_mat4(w, mn);
				x[k]      = ta != NULL ? ta[i * 2] / 32767.0f * md->scale_tex * res : 0.0f;
				y[k]      = ta != NULL ? ta[i * 2 + 1] / 32767.0f * md->scale_tex * res : 0.0f;
				r->min[0] = fminf(r->min[0], p[k].x);
				r->min[1] = fminf(r->min[1], p[k].y);
				r->min[2] = fminf(r->min[2], p[k].z);
				r->max[0] = fmaxf(r->max[0], p[k].x);
				r->max[1] = fmaxf(r->max[1], p[k].y);
				r->max[2] = fmaxf(r->max[2], p[k].z);
			}
			if (rays) {
				f32 *dst = tris + tri_i * 9;
				for (i32 k = 0; k < 3; ++k) {
					dst[k * 3 + 0] = p[k].x;
					dst[k * 3 + 1] = p[k].y;
					dst[k * 3 + 2] = p[k].z;
				}
				tri_objs[tri_i] = oi;
				tri_i++;
			}

			f32 area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
			if (fabsf(area) < 1e-12f) {
				continue;
			}
			i32 x0 = (i32)floorf(fminf(x[0], fminf(x[1], x[2])));
			i32 x1 = (i32)ceilf(fmaxf(x[0], fmaxf(x[1], x[2])));
			i32 y0 = (i32)floorf(fminf(y[0], fminf(y[1], y[2])));
			i32 y1 = (i32)ceilf(fmaxf(y[0], fmaxf(y[1], y[2])));
			x0     = x0 < 0 ? 0 : x0;
			y0     = y0 < 0 ? 0 : y0;
			x1     = x1 > res - 1 ? res - 1 : x1;
			y1     = y1 > res - 1 ? res - 1 : y1;
			for (i32 py = y0; py <= y1; ++py) {
				for (i32 px = x0; px <= x1; ++px) {
					f32 cx = px + 0.5f;
					f32 cy = py + 0.5f;
					f32 b0 = ((x[1] - cx) * (y[2] - cy) - (x[2] - cx) * (y[1] - cy)) / area;
					f32 b1 = ((x[2] - cx) * (y[0] - cy) - (x[0] - cx) * (y[2] - cy)) / area;
					f32 b2 = 1.0f - b0 - b1;
					if (b0 < -1e-4f || b1 < -1e-4f || b2 < -1e-4f) {
						continue;
					}
					i32  ti       = py * res + px;
					f32 *pp       = r->pos + ti * 3;
					pp[0]         = p[0].x * b0 + p[1].x * b1 + p[2].x * b2;
					pp[1]         = p[0].y * b0 + p[1].y * b1 + p[2].y * b2;
					pp[2]         = p[0].z * b0 + p[1].z * b1 + p[2].z * b2;
					vec4_t nn     = vec4_norm((vec4_t){n[0].x * b0 + n[1].x * b1 + n[2].x * b2, n[0].y * b0 + n[1].y * b1 + n[2].y * b2,
					                                   n[0].z * b0 + n[1].z * b1 + n[2].z * b2, 0.0});
					f32   *np     = r->nor + ti * 3;
					np[0]         = nn.x;
					np[1]         = nn.y;
					np[2]         = nn.z;
					r->object[ti] = oi;
					if (r->color != NULL) {
						u8 *c = r->color + ti * 4;
						if (ca != NULL) {
							i32 i0 = ind->buffer[t];
							i32 i1 = ind->buffer[t + 1];
							i32 i2 = ind->buffer[t + 2];
							for (i32 k = 0; k < 4; ++k) {
								f32 v = (ca[i0 * 4 + k] * b0 + ca[i1 * 4 + k] * b1 + ca[i2 * 4 + k] * b2) / 32767.0f;
								v     = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
								c[k]  = (u8)(v * 255.0f + 0.5f);
							}
						}
						else {
							c[0] = c[1] = c[2] = c[3] = 255;
						}
					}
				}
			}
		}
	}

	if (r->min[0] > r->max[0]) { // No geometry
		for (i32 a = 0; a < 3; ++a) {
			r->min[a] = 0.0;
			r->max[a] = 0.0;
		}
	}
	f32 dx  = r->max[0] - r->min[0];
	f32 dy  = r->max[1] - r->min[1];
	f32 dz  = r->max[2] - r->min[2];
	r->size = sqrtf(dx * dx + dy * dy + dz * dz);
	if (r->size < 1e-6f) {
		r->size = 1.0;
	}
	if (rays) {
		util_bvh_build(&r->bvh, tris, tri_objs, tri_i);
	}
	return r;
}

static void util_bake_id_color(i32 index, f32 *c) {
	// Golden ratio hues stay apart for neighbouring indices
	f32 h = index * 0.61803398875f;
	h -= floorf(h);
	f32 s         = 0.7;
	f32 v         = 0.95;
	f32 h6        = h * 6.0f;
	i32 hi        = (i32)h6 % 6;
	f32 f         = h6 - floorf(h6);
	f32 p         = v * (1.0f - s);
	f32 q         = v * (1.0f - s * f);
	f32 t         = v * (1.0f - s * (1.0f - f));
	f32 rgb[6][3] = {{v, t, p}, {q, v, p}, {p, v, t}, {p, q, v}, {t, p, v}, {v, p, q}};
	c[0]          = rgb[hi][0];
	c[1]          = rgb[hi][1];
	c[2]          = rgb[hi][2];
}

static f32 util_bake_curvature(bake_job_t *job, i32 x, i32 y) {
	util_bake_raster_t *r     = job->raster;
	i32                 res   = r->res;
	i32                 ti    = y * res + x;
	f32                *pa    = r->pos + ti * 3;
	f32                *na    = r->nor + ti * 3;
	f32                 sum   = 0.0;
	i32                 count = 0;
	i32                 nx[4] = {x - 1, x + 1, x, x};
	i32                 ny[4] = {y, y, y - 1, y + 1};
	for (i32 k = 0; k < 4; ++k) {
		if (nx[k] < 0 || ny[k] < 0 || nx[k] >= res || ny[k] >= res) {
			continue;
		}
		i32 tj = ny[k] * res + nx[k];
		if (r->object[tj] != r->object[ti]) {
			continue;
		}
		f32 *pb    = r->pos + tj * 3;
		f32 *nb    = r->nor + tj * 3;
		f32  dp[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
		f32  len2  = dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2];
		if (len2 < 1e-20f) {
			continue;
		}
		// Normals that spread apart along the surface are convex, positive
		sum += ((nb[0] - na[0]) * dp[0] + (nb[1] - na[1]) * dp[1] + (nb[2] - na[2]) * dp[2]) / len2;
		count++;
	}
	if (count == 0) {
		return 0.5;
	}
	f32 k = sum / count * r->size * job->settings.curvature_strength * 0.1f;
	k     = k < -1.0f ? -1.0f : k > 1.0f ? 1.0f : k;
	return 0.5f + 0.5f * k;
}

static void util_bake_texel(bake_job_t *job, i32 x, i32 y, f32 *out) {
	util_bake_raster_t *r   = job->raster;
	i32                 ti  = y * r->res + x;
	f32                *p   = r->pos + ti * 3;
	f32                *n   = r->nor + ti * 3;
	i32                 obj = r->object[ti];
	out[3]                  = 1.0;

	switch (job->slot) {
	case MESH_MAP_NORMAL: // Flat until a high poly is baked into it
		out[0] = 0.5;
		out[1] = 0.5;
		out[2] = 1.0;
		break;
	case MESH_MAP_WORLD_NORMAL:
		for (i32 k = 0; k < 3; ++k) {
			out[k] = n[k] * 0.5f + 0.5f;
		}
		break;
	case MESH_MAP_OCCLUSION:
	case MESH_MAP_BENT_NORMAL: { // Unoccluded directions of the occlusion rays average to the bent normal
		i32 count   = job->settings.occlusion_samples > 0 ? job->settings.occlusion_samples : 1;
		f32 dist    = job->settings.occlusion_distance * r->size;
		f32 eps     = r->size * 2e-4f;
		f32 o[3]    = {p[0] + n[0] * eps, p[1] + n[1] * eps, p[2] + n[2] * eps};
		f32 r1      = util_bake_hash((u32)ti * 2 + 1);
		f32 r2      = util_bake_hash((u32)ti * 2 + 2);
		i32 hits    = 0;
		f32 bent[3] = {0.0, 0.0, 0.0};
		for (i32 i = 0; i < count; ++i) {
			f32       d[3];
			bvh_hit_t hit;
			util_bake_hemisphere(n, i, count, r1, r2, d);
			if (util_bvh_raycast(&r->bvh, o, d, dist, true, &hit)) {
				hits++;
			}
			else {
				bent[0] += d[0];
				bent[1] += d[1];
				bent[2] += d[2];
			}
		}
		if (job->slot == MESH_MAP_OCCLUSION) {
			out[0] = out[1] = out[2] = 1.0f - (f32)hits / count;
		}
		else {
			f32 len = sqrtf(bent[0] * bent[0] + bent[1] * bent[1] + bent[2] * bent[2]);
			for (i32 k = 0; k < 3; ++k) {
				out[k] = (len > 1e-6f ? bent[k] / len : n[k]) * 0.5f + 0.5f;
			}
		}
		break;
	}
	case MESH_MAP_THICKNESS: {
		i32 count = job->settings.thickness_samples > 0 ? job->settings.thickness_samples : 1;
		f32 dist  = job->settings.thickness_distance * r->size;
		f32 eps   = r->size * 2e-4f;
		f32 in[3] = {-n[0], -n[1], -n[2]};
		f32 o[3]  = {p[0] + in[0] * eps, p[1] + in[1] * eps, p[2] + in[2] * eps};
		f32 r1    = util_bake_hash((u32)ti * 2 + 1);
		f32 r2    = util_bake_hash((u32)ti * 2 + 2);
		f32 sum   = 0.0;
		for (i32 i = 0; i < count; ++i) {
			f32       d[3];
			bvh_hit_t hit;
			util_bake_hemisphere(in, i, count, r1, r2, d);
			sum += util_bvh_raycast(&r->bvh, o, d, dist, false, &hit) ? hit.t : dist;
		}
		out[0] = out[1] = out[2] = sum / count / dist; // White is thick
		break;
	}
	case MESH_MAP_ID:
		if (job->settings.id_source == MESH_MAP_ID_VERTEX_COLOR && r->color != NULL) {
			u8 *c  = r->color + ti * 4;
			out[0] = c[0] / 255.0f;
			out[1] = c[1] / 255.0f;
			out[2] = c[2] / 255.0f;
		}
		else {
			util_bake_id_color(obj, out);
		}
		break;
	case MESH_MAP_CURVATURE:
		out[0] = out[1] = out[2] = util_bake_curvature(job, x, y);
		break;
	case MESH_MAP_POSITION:
		for (i32 k = 0; k < 3; ++k) {
			f32 ext = r->max[k] - r->min[k];
			out[k]  = ext > 1e-6f ? (p[k] - r->min[k]) / ext : 0.5f;
		}
		break;
	case MESH_MAP_OPACITY:
		out[0] = out[1] = out[2] = 1.0;
		break;
	}
}

static void util_bake_rows_thread(void *param) {
	bake_job_t *job = param;
	i32         res = job->raster->res;
	i32         end = job->rows_done + job->rows_per_frame;
	end             = end > res ? res : end;
	for (;;) {
		i32 y = iron_atomic_increment(&job->next_row);
		if (y >= end) {
			break;
		}
		for (i32 x = 0; x < res; ++x) {
			i32 ti = y * res + x;
			if (job->raster->object[ti] >= 0) {
				util_bake_texel(job, x, y, job->out + ti * 4);
			}
		}
	}
}

static bool util_bake_slot_casts_rays(i32 slot) {
	return slot == MESH_MAP_OCCLUSION || slot == MESH_MAP_BENT_NORMAL || slot == MESH_MAP_THICKNESS;
}

// Grows the uv islands by copying the average of the filled neighbours, the rest gets the default value
static void util_bake_dilate(bake_job_t *job) {
	i32  res  = job->raster->res;
	f32 *px   = job->out;
	u8  *mask = malloc((size_t)res * res);
	u8  *next = malloc((size_t)res * res);
	for (i32 i = 0; i < res * res; ++i) {
		mask[i] = job->raster->object[i] >= 0;
	}
	for (i32 pass = 0; pass < job->settings.dilation; ++pass) {
		memcpy(next, mask, (size_t)res * res);
		bool grown = false;
		for (i32 y = 0; y < res; ++y) {
			for (i32 x = 0; x < res; ++x) {
				if (mask[y * res + x]) {
					continue;
				}
				f32 sum[4] = {0.0, 0.0, 0.0, 0.0};
				i32 n      = 0;
				for (i32 dy = -1; dy <= 1; ++dy) {
					for (i32 dx = -1; dx <= 1; ++dx) {
						i32 sx = x + dx;
						i32 sy = y + dy;
						if (sx < 0 || sy < 0 || sx >= res || sy >= res || !mask[sy * res + sx]) {
							continue;
						}
						f32 *s = px + (sy * res + sx) * 4;
						for (i32 k = 0; k < 4; ++k) {
							sum[k] += s[k];
						}
						n++;
					}
				}
				if (n > 0) {
					f32 *d = px + (y * res + x) * 4;
					for (i32 k = 0; k < 4; ++k) {
						d[k] = sum[k] / n;
					}
					next[y * res + x] = 1;
					grown             = true;
				}
			}
		}
		memcpy(mask, next, (size_t)res * res);
		if (!grown) {
			break;
		}
	}
	f32 def[4];
	util_bake_default_value(job->slot, def);
	for (i32 i = 0; i < res * res; ++i) {
		if (!mask[i]) {
			memcpy(px + i * 4, def, sizeof(def));
		}
	}
	free(mask);
	free(next);
}

static gpu_texture_t *util_bake_upload(i32 slot, f32 *px, i32 res) {
	bool           vector = util_bake_slot_is_vector(slot);
	gpu_texture_t *rt     = gpu_create_render_target(res, res, vector ? GPU_TEXTURE_FORMAT_RGBA64 : GPU_TEXTURE_FORMAT_RGBA32);
	gpu_texture_t *src    = malloc(sizeof(gpu_texture_t));
	src->buffer           = NULL;
	if (vector) {
		gpu_texture_init_from_bytes(src, px, res, res, GPU_TEXTURE_FORMAT_RGBA128, false);
	}
	else {
		u8 *b = malloc((size_t)res * res * 4);
		for (i32 i = 0; i < res * res * 4; ++i) {
			f32 v = px[i];
			v     = v < 0.0f ? 0.0f : v > 1.0f ? 1.0f : v;
			b[i]  = (u8)(v * 255.0f + 0.5f);
		}
		gpu_texture_init_from_bytes(src, b, res, res, GPU_TEXTURE_FORMAT_RGBA32, false);
		free(b);
	}
	draw_begin(rt, false, 0);
	draw_set_pipeline(vector ? pipes_copy64 : pipes_copy); // The pipeline matches the target format
	draw_image(src, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();
	gpu_delete_texture(src);
	return rt;
}

// Materials with a Mesh Map node read the new texture
// Layers, material icons and the selected node preview that read a mesh map, whichever material is selected
void util_bake_maps_changed() {
	layers_update_mesh_map_layers();
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (layers_material_reads_mesh_maps(m)) {
			g_context->material = m;
			util_render_make_material_preview();
		}
	}
	g_context->material = _material;
	if (layers_material_reads_mesh_maps(_material)) {
		make_material_parse_paint_material(true);
	}
	ui_nodes_last_node_selected_id = -1; // Remake the preview of the selected node
	g_context->ddirty              = 2;
	ui_dock_redraw(TAB_AREA_SIDEBAR1);
	if (ui_nodes_hwnd != NULL) {
		ui_nodes_hwnd->redraws = 2;
	}
}

static void util_bake_set_map(i32 slot, gpu_texture_t *tex, bool history) {
	if (history) {
		history_step_t *step = history_push(HISTORY_ACTION_BAKE_MESH_MAP);
		step->mesh_map_slot  = slot;
		step->mesh_map_tex   = util_bake_maps[slot]; // Kept for undo
		if (tex == NULL) {
			step->name = tr("Clear Mesh Map");
		}
	}
	else if (util_bake_maps[slot] != NULL) {
		gpu_delete_texture(util_bake_maps[slot]);
	}
	util_bake_maps[slot] = tex;
}

// Undo and redo swap the mesh map of the slot with the one kept by the step
void util_bake_swap_history(history_step_t *step) {
	i32 slot = step->mesh_map_slot;
	if (slot < 0 || slot >= MESH_MAP_COUNT) {
		return;
	}
	gpu_texture_t *tex   = util_bake_maps[slot];
	util_bake_maps[slot] = step->mesh_map_tex;
	step->mesh_map_tex   = tex;
	util_bake_maps_changed();
}

void util_bake_clear(i32 slot) {
	if (slot < 0 || slot >= MESH_MAP_COUNT || util_bake_maps[slot] == NULL) {
		return;
	}
	util_bake_set_map(slot, NULL, true);
	util_bake_maps_changed();
}

static void util_bake_job_free(bake_job_t *job) {
	util_bake_raster_free(job->raster);
	free(job->out);
	free(job->data);
	free(job);
}

static void util_bake_next_job() {
	util_bake_job = util_bake_jobs[0];
	for (i32 i = 1; i < util_bake_jobs_count; ++i) {
		util_bake_jobs[i - 1] = util_bake_jobs[i];
	}
	util_bake_jobs_count--;
}

static void util_bake_finish(bake_job_t *job) {
	util_bake_job = NULL;
	if (util_bake_jobs_count == 0) {
		sys_remove_update(util_bake_update);
	}
	if (job->done != NULL) {
		job->done(job->done_data);
	}
	util_bake_job_free(job);
}

static void util_bake_step_rows(bake_job_t *job) {
	i32    res    = job->raster->res;
	double start  = iron_time();
	i32    end    = job->rows_done + job->rows_per_frame;
	end           = end > res ? res : end;
	job->next_row = job->rows_done;

	i32 thread_count = iron_hardware_threads();
#ifdef IRON_WASM
	thread_count = 1;
#endif
	thread_count = thread_count > UTIL_BAKE_THREADS ? UTIL_BAKE_THREADS : thread_count;
	thread_count = thread_count > end - job->rows_done ? end - job->rows_done : thread_count;
	if (thread_count <= 1) {
		util_bake_rows_thread(job);
	}
	else {
		iron_thread_t threads[UTIL_BAKE_THREADS];
		for (i32 i = 0; i < thread_count; ++i) {
			iron_thread_init(&threads[i], util_bake_rows_thread, job);
		}
		for (i32 i = 0; i < thread_count; ++i) {
			iron_thread_wait_and_destroy(&threads[i]);
		}
	}
	i32 rows       = end - job->rows_done;
	job->rows_done = end;
	job->progress  = (f32)job->rows_done / res;

	// Aim at a fixed amount of work per frame
	f64 elapsed = iron_time() - start;
	if (elapsed > 1e-4) {
		f64 scale           = UTIL_BAKE_FRAME_BUDGET / elapsed;
		scale               = scale > 4.0 ? 4.0 : scale < 0.25 ? 0.25 : scale;
		job->rows_per_frame = (i32)(rows * scale);
	}
	else {
		job->rows_per_frame = rows * 4;
	}
	job->rows_per_frame = job->rows_per_frame < 1 ? 1 : job->rows_per_frame;
}

void util_bake_update(void *_) {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	if (util_bake_job == NULL && util_bake_jobs_count > 0) {
		util_bake_next_job();
	}
	bake_job_t *job = util_bake_job;
	ui_dock_redraw(TAB_AREA_SIDEBAR1); // Progress in the Baking tab
	if (job == NULL) {
		sys_remove_update(util_bake_update);
	}
	else if (job->slot < 0) { // Custom job
		if (job->step(job)) {
			util_bake_finish(job);
		}
	}
	else if (job->raster == NULL) {
		i32 res = job->settings.res > 0 ? job->settings.res : 1;
		job->raster =
		    util_bake_raster_create(res, job->slot == MESH_MAP_ID && job->settings.id_source == MESH_MAP_ID_VERTEX_COLOR, util_bake_slot_casts_rays(job->slot));
		job->out            = calloc((size_t)res * res * 4, sizeof(f32));
		job->rows_per_frame = util_bake_slot_casts_rays(job->slot) ? 1 : res;
	}
	else if (job->rows_done < job->raster->res) {
		util_bake_step_rows(job);
	}
	else {
		util_bake_dilate(job);
		gpu_texture_t *tex = util_bake_upload(job->slot, job->out, job->raster->res);
		util_bake_set_map(job->slot, tex, true);
		util_bake_maps_changed();
		util_bake_finish(job);
	}

	if (in_use) {
		draw_begin(current, false, 0);
	}
}

static void util_bake_push(bake_job_t *job) {
	if (util_bake_job == NULL && util_bake_jobs_count == 0) {
		sys_notify_on_update(util_bake_update, NULL);
	}
	util_bake_jobs                         = realloc(util_bake_jobs, sizeof(bake_job_t *) * (util_bake_jobs_count + 1));
	util_bake_jobs[util_bake_jobs_count++] = job;
}

// Queues a bake of the slot with the current settings, done is called once the slot holds the new map.
// A slot already waiting in the queue is not queued twice.
bool util_bake_run(i32 slot, void (*done)(void *), void *done_data) {
	if (slot < 0 || slot >= MESH_MAP_COUNT) {
		return false;
	}
	if (util_bake_state(slot, NULL) == BAKE_STATE_QUEUED && done == NULL) {
		return true;
	}
	bake_job_t *job = calloc(1, sizeof(bake_job_t));
	job->slot       = slot;
	job->settings   = util_bake_settings;
	job->done       = done;
	job->done_data  = done_data;
	util_bake_push(job);
	return true;
}

// Done is called after the last slot
static void util_bake_run_mask(i32 maps, void (*done)(void *), void *done_data) {
	i32 last = -1;
	for (i32 i = 0; i < MESH_MAP_COUNT; ++i) {
		if ((maps >> i) & 1) {
			last = i;
		}
	}
	for (i32 i = 0; i <= last; ++i) {
		if ((maps >> i) & 1) {
			util_bake_run(i, i == last ? done : NULL, done_data);
		}
	}
}

void util_bake_run_all(void (*done)(void *), void *done_data) {
	util_bake_run_mask((1 << MESH_MAP_COUNT) - 1, done, done_data);
}

// The slots checked in the Baking tab
void util_bake_run_selected(void (*done)(void *), void *done_data) {
	util_bake_run_mask(util_bake_settings.maps, done, done_data);
}

// A job that drives another bake, like the Bake Texture node, queued with the mesh maps. The job frees data with free().
void util_bake_run_custom(bool (*step)(bake_job_t *), void (*cancel)(bake_job_t *), void *data, void *owner, void (*done)(void *), void *done_data) {
	bake_job_t *job = calloc(1, sizeof(bake_job_t));
	job->slot       = -1;
	job->owner      = owner;
	job->step       = step;
	job->cancel     = cancel;
	job->data       = data;
	job->done       = done;
	job->done_data  = done_data;
	util_bake_push(job);
}

static bool util_bake_job_matches(bake_job_t *job, i32 slot, void *owner) {
	return slot >= 0 ? job->slot == slot : job->slot < 0 && job->owner == owner;
}

// State of a mesh map slot, or of the custom job of an owner when slot is -1
i32 util_bake_state(i32 slot, void *owner) {
	if (util_bake_job != NULL && util_bake_job_matches(util_bake_job, slot, owner)) {
		return BAKE_STATE_RUNNING;
	}
	for (i32 i = 0; i < util_bake_jobs_count; ++i) {
		if (util_bake_job_matches(util_bake_jobs[i], slot, owner)) {
			return BAKE_STATE_QUEUED;
		}
	}
	return BAKE_STATE_IDLE;
}

// Cancels the jobs of a slot, or of an owner when slot is -1, and leaves the rest of the queue
void util_bake_cancel_job(i32 slot, void *owner) {
	i32 kept = 0;
	for (i32 i = 0; i < util_bake_jobs_count; ++i) {
		if (util_bake_job_matches(util_bake_jobs[i], slot, owner)) {
			util_bake_job_free(util_bake_jobs[i]);
		}
		else {
			util_bake_jobs[kept++] = util_bake_jobs[i];
		}
	}
	util_bake_jobs_count = kept;
	bake_job_t *job      = util_bake_job;
	if (job != NULL && util_bake_job_matches(job, slot, owner)) {
		if (job->slot < 0) { // Finishes on its own
			if (!job->cancelled && job->cancel != NULL) {
				job->cancel(job);
			}
			job->cancelled = true;
			return;
		}
		util_bake_job_free(job);
		util_bake_job = NULL;
	}
	if (util_bake_job == NULL && util_bake_jobs_count == 0) {
		sys_remove_update(util_bake_update);
	}
}

// Jobs waiting behind the running one
i32 util_bake_queued() {
	return util_bake_jobs_count;
}

bool util_bake_busy() {
	return util_bake_job != NULL || util_bake_jobs_count > 0;
}

// Slot of the running job, -1 for none or a custom job
i32 util_bake_current_slot() {
	return util_bake_job != NULL ? util_bake_job->slot : -1;
}

void *util_bake_current_data() {
	return util_bake_job != NULL ? util_bake_job->data : NULL;
}

f32 util_bake_progress() {
	return util_bake_job != NULL ? util_bake_job->progress : 0.0;
}

void util_bake_set_progress(bake_job_t *job, f32 progress) {
	job->progress = progress;
}

void *util_bake_job_data(bake_job_t *job) {
	return job->data;
}

bake_settings_t *util_bake_get_settings() {
	return &util_bake_settings;
}

// Drops the queued jobs and the running mesh map, a running custom job is asked to stop and finishes on its own
void util_bake_cancel() {
	for (i32 i = 0; i < util_bake_jobs_count; ++i) {
		util_bake_job_free(util_bake_jobs[i]);
	}
	util_bake_jobs_count = 0;
	if (util_bake_job != NULL) {
		if (util_bake_job->slot < 0) {
			if (!util_bake_job->cancelled && util_bake_job->cancel != NULL) {
				util_bake_job->cancel(util_bake_job);
			}
			util_bake_job->cancelled = true;
			return;
		}
		util_bake_job_free(util_bake_job);
		util_bake_job = NULL;
	}
	sys_remove_update(util_bake_update);
}

// New or opened project
void util_bake_reset() {
	util_bake_cancel();
	util_bake_settings = (bake_settings_t)UTIL_BAKE_DEFAULT_SETTINGS;
	for (i32 i = 0; i < MESH_MAP_COUNT; ++i) {
		if (util_bake_maps[i] != NULL) {
			gpu_delete_texture(util_bake_maps[i]);
			util_bake_maps[i] = NULL;
		}
	}
}

// Pixels of a baked slot as rgba floats in 0-1, NULL when the slot is empty
f32_array_t *util_bake_pixels(i32 slot) {
	gpu_texture_t *tex = util_bake_mesh_map(slot);
	if (tex == NULL) {
		return NULL;
	}
	buffer_t    *b   = gpu_get_texture_pixels(tex);
	i32          n   = tex->width * tex->height;
	f32_array_t *out = f32_array_create(n * 4);
	if (tex->format == GPU_TEXTURE_FORMAT_RGBA64) {
		u16 *h = (u16 *)b->buffer;
		for (i32 i = 0; i < n * 4; ++i) {
			out->buffer[i] = render_path_raytrace_half_to_float(h[i]);
		}
	}
	else {
		for (i32 i = 0; i < n; ++i) {
			for (i32 k = 0; k < 4; ++k) {
#ifdef IRON_BGRA
				i32 src = k < 3 ? 2 - k : 3;
#else
				i32 src = k;
#endif
				out->buffer[i * 4 + k] = b->buffer[i * 4 + src] / 255.0f;
			}
		}
	}
	return out;
}

char *util_bake_settings_encode() {
	bake_settings_t *s = &util_bake_settings;
	return string("{\"res\":%d,\"dilation\":%d,\"occlusion_samples\":%d,\"occlusion_distance\":%f,\"thickness_samples\":%d,\"thickness_distance\":%f,"
	              "\"curvature_strength\":%f,\"id_source\":%d,\"maps\":%d}",
	              s->res, s->dilation, s->occlusion_samples, s->occlusion_distance, s->thickness_samples, s->thickness_distance, s->curvature_strength,
	              s->id_source, s->maps);
}

static void util_bake_settings_read_i32(any_map_t *m, char *key, i32 *v) {
	char *s = any_map_get(m, key);
	if (s != NULL) {
		*v = parse_int(s);
	}
}

static void util_bake_settings_read_f32(any_map_t *m, char *key, f32 *v) {
	char *s = any_map_get(m, key);
	if (s != NULL) {
		*v = parse_float(s);
	}
}

// Missing keys keep their current value
void util_bake_settings_decode(char *json) {
	if (json == NULL || json[0] == '\0') {
		return;
	}
	bake_settings_t *s = &util_bake_settings;
	any_map_t       *m = json_parse_to_map(json);
	util_bake_settings_read_i32(m, "res", &s->res);
	util_bake_settings_read_i32(m, "dilation", &s->dilation);
	util_bake_settings_read_i32(m, "occlusion_samples", &s->occlusion_samples);
	util_bake_settings_read_f32(m, "occlusion_distance", &s->occlusion_distance);
	util_bake_settings_read_i32(m, "thickness_samples", &s->thickness_samples);
	util_bake_settings_read_f32(m, "thickness_distance", &s->thickness_distance);
	util_bake_settings_read_f32(m, "curvature_strength", &s->curvature_strength);
	util_bake_settings_read_i32(m, "id_source", &s->id_source);
	util_bake_settings_read_i32(m, "maps", &s->maps);
	s->res       = s->res < 1 ? 1 : s->res > 16384 ? 16384 : s->res;
	s->dilation  = s->dilation < 0 ? 0 : s->dilation;
	s->id_source = s->id_source == MESH_MAP_ID_VERTEX_COLOR ? MESH_MAP_ID_VERTEX_COLOR : MESH_MAP_ID_OBJECT;
	s->maps &= (1 << MESH_MAP_COUNT) - 1;
}

// Fills the version 23 fields of the project before it is encoded
void util_bake_export(project_t *project) {
	project->bake_settings = util_bake_settings_encode();
	project->mesh_maps     = any_array_create_from_raw((void *[]){}, 0);
	for (i32 slot = 0; slot < MESH_MAP_COUNT; ++slot) {
		gpu_texture_t *tex = util_bake_maps[slot];
		if (tex == NULL) {
			continue;
		}
		buffer_t *pixels = gpu_get_texture_pixels(tex);
		bool      vector = tex->format == GPU_TEXTURE_FORMAT_RGBA64;
#ifdef IRON_BGRA
		if (!vector) { // Stored as rgba, the readback is a copy that the next readback overwrites
			for (i32 i = 0; i + 3 < pixels->length; i += 4) {
				u8 b                 = pixels->buffer[i];
				pixels->buffer[i]     = pixels->buffer[i + 2];
				pixels->buffer[i + 2] = b;
			}
		}
#endif
		mesh_map_data_t *d = ALLOC_INIT(mesh_map_data_t, {.slot = slot, .set = 0, .res = tex->width, .bpp = vector ? 16 : 8, .texture = lz4_encode(pixels)});
		any_array_push(project->mesh_maps, d);
		// Keep no cpu copy of the map after saving
		free(tex->buffer->buffer);
		free(tex->buffer);
		tex->buffer = NULL;
	}
	if (project->mesh_maps->length == 0) {
		project->mesh_maps = NULL;
	}
}

void util_bake_export_free(project_t *project) {
	if (project->mesh_maps != NULL) {
		for (i32 i = 0; i < project->mesh_maps->length; ++i) {
			buffer_t *b = project->mesh_maps->buffer[i]->texture;
			free(b->buffer);
			free(b);
		}
	}
	project->mesh_maps = NULL;
}

// Restores the mesh maps and settings of an opened project
void util_bake_import(project_t *project) {
	util_bake_settings_decode(project->bake_settings);
	if (project->mesh_maps == NULL) {
		return;
	}
	for (i32 i = 0; i < project->mesh_maps->length; ++i) {
		mesh_map_data_t *d = project->mesh_maps->buffer[i];
		if (d->slot < 0 || d->slot >= MESH_MAP_COUNT || d->res <= 0 || d->texture == NULL) {
			continue;
		}
		bool           vector = d->bpp == 16;
		u32            size   = (u32)d->res * d->res * (vector ? 8 : 4);
		buffer_t      *pixels = lz4_decode(d->texture, size);
		gpu_texture_t *src    = gpu_create_texture_from_bytes_raw(pixels, d->res, d->res, vector ? GPU_TEXTURE_FORMAT_RGBA64 : GPU_TEXTURE_FORMAT_RGBA32);
		gpu_texture_t *rt     = gpu_create_render_target(d->res, d->res, vector ? GPU_TEXTURE_FORMAT_RGBA64 : GPU_TEXTURE_FORMAT_RGBA32);
		draw_begin(rt, false, 0);
		draw_set_pipeline(vector ? pipes_copy64 : pipes_copy);
		draw_image(src, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();
		gpu_delete_texture(src);
		array_free(pixels);
		free(pixels);
		util_bake_set_map(d->slot, rt, false);
	}
}
