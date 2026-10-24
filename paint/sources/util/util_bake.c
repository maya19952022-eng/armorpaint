#include "../global.h"

// Mesh maps: textures baked from the geometry of the project, one per mesh_map_slot_t, read by the Mesh Map node.
// Baked on the cpu in uv space by a queue of jobs that run a slice of rows each frame.
// See base/docs/features/mesh_maps.md.

// Triangles of the low or the high poly objects in world space
typedef struct util_bake_mesh {
	i32  count;
	f32 *pos;    // 9 floats per triangle
	f32 *nor;    // 9 floats per triangle, vertex normals
	f32 *ray;    // 9 floats per triangle, ray directions: the vertex normals, or averaged over vertices at the same place
	f32 *uv;     // 6 floats per triangle, in texels of the map
	i32 *object; // Paint object index per triangle
	i32 *group;  // Per triangle, objects with the same name without _low or _high share a group
	u8  *col;    // 12 bytes per triangle, vertex colors, NULL when not needed
	i32 *island; // UV island per triangle, low poly only
	f32 *curv;   // 3 per triangle, vertex curvature, NULL when not needed
} util_bake_mesh_t;

typedef struct util_bake_raster {
	i32              res;
	i32             *tri;    // Low poly triangle at each texel center, -1 outside the uv islands
	util_bake_mesh_t low;    // The objects that are not high poly
	util_bake_mesh_t high;   // Empty without high poly objects
	f32              min[3]; // Bounds of all the geometry
	f32              max[3];
	f32              size; // Diagonal of the bounds
	bvh_t            bvh;  // The high poly, or the low poly for the slots that cast rays without one
	f32             *skew; // Skew map per texel of its layer, NULL without one or without high poly
	f32             *cage; // Cage map per texel of its layer, -1 to 1
	i32              role_w;
	i32              role_h;
	u8              *hard; // Per low poly triangle, bit k: the edge from corner k to k + 1 is hard, NULL without edge protection
} util_bake_raster_t;

// What one sample of a map sees
typedef struct util_bake_surface {
	f32  p[3];
	f32  n[3];
	f32  col[3];
	i32  object; // Paint object index
	i32  tri;    // Triangle of the low or high mesh
	f32  b[3];   // Barycentrics in it
	bool high;   // On the high poly
	bool hit;    // False where the ray finds no high poly
	f32  lp[3];  // Low poly point and normal
	f32  ln[3];
	i32  ltri;
} util_bake_surface_t;

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
	void *owner;         // Custom job, the node or tool that started it
	bool  automatic;     // Queued by auto rebake, leaves no history step
	bool  quick;         // Preview at a quarter of the resolution
	u32   settings_hash; // util_bake_settings_hash of the settings it was queued with
	u32   sig;           // Settings and scene it bakes, set when it starts
	u32   queued_sig;    // Auto rebake: the signature it was queued for
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
	.maps               = (1 << MESH_MAP_COUNT) - 1, \
	.aa                 = 1,            \
	.front_distance     = 0.01,         \
	.rear_distance      = 0.01,         \
	.average_normals    = true,         \
	.ignore_backface    = true,         \
	.match_by_name      = false,        \
	.high_polys         = "",           \
	.curvature_mode     = MESH_MAP_CURVATURE_NORMALS, \
	.auto_rebake        = false,        \
	.quick_rebake       = true,         \
	.cage               = BAKE_CAGE_DISTANCE, \
	.skew_base          = BAKE_SKEW_BASE_MESH, \
	.edge_protection    = true,         \
	.paint_value        = 1.0,          \
	.cage_range         = 0.05          \
}
// clang-format on

bake_settings_t util_bake_settings = UTIL_BAKE_DEFAULT_SETTINGS;

static gpu_texture_t *util_bake_maps[MESH_MAP_COUNT];
static gpu_texture_t *util_bake_defaults[MESH_MAP_COUNT];
static bake_job_t   **util_bake_jobs       = NULL;
static i32            util_bake_jobs_count = 0;
static bake_job_t    *util_bake_job        = NULL;

// What each slot was baked from: 0 unknown (an older project), even for a full bake, odd for a quick preview
static u32 util_bake_sigs[MESH_MAP_COUNT];
static u32 util_bake_skip_sigs[MESH_MAP_COUNT]; // A cancelled bake is not started again by auto rebake for the same signature
static u32 util_bake_seen_sigs[MESH_MAP_COUNT]; // Auto rebake waits until the signature stops changing
static f64 util_bake_seen_time[MESH_MAP_COUNT];
static f64 util_bake_auto_last = 0.0;

#define UTIL_BAKE_THREADS      16
#define UTIL_BAKE_FRAME_BUDGET 0.03 // Seconds of work per frame
#define UTIL_BAKE_HASH_SAMPLES 1024 // Values hashed per vertex array, the signature of a mesh
#define UTIL_BAKE_AUTO_DELAY   0.5  // Seconds without changes before auto rebake starts
#define UTIL_BAKE_QUICK_MIN    512  // Resolutions below this skip the quick preview

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

static void util_bake_mesh_free(util_bake_mesh_t *m) {
	free(m->pos);
	free(m->nor);
	free(m->ray);
	free(m->uv);
	free(m->object);
	free(m->group);
	free(m->col);
	free(m->island);
	free(m->curv);
	*m = (util_bake_mesh_t){0};
}

static void util_bake_raster_free(util_bake_raster_t *r) {
	if (r == NULL) {
		return;
	}
	free(r->tri);
	util_bake_mesh_free(&r->low);
	util_bake_mesh_free(&r->high);
	util_bvh_free(&r->bvh);
	free(r->skew);
	free(r->cage);
	free(r->hard);
	free(r);
}

static bool util_bake_ends_with(char *s, char *suffix) {
	i32 n = string_length(s);
	i32 m = string_length(suffix);
	if (n < m) {
		return false;
	}
	for (i32 i = 0; i < m; ++i) {
		char c = s[n - m + i];
		c      = c >= 'A' && c <= 'Z' ? c + 32 : c;
		if (c != suffix[i]) {
			return false;
		}
	}
	return true;
}

// Whether name is one of the lines of list
static bool util_bake_list_has(char *list, char *name) {
	i32 n = string_length(name);
	for (char *line = list; line != NULL && *line != '\0';) {
		char *end = strchr(line, '\n');
		i32   len = end != NULL ? (i32)(end - line) : string_length(line);
		if (len == n && strncmp(line, name, n) == 0) {
			return true;
		}
		line = end != NULL ? end + 1 : NULL;
	}
	return false;
}

// In the high poly list, not counting the objects that match by name
bool util_bake_listed_high_poly(char *name) {
	return util_bake_list_has(util_bake_settings.high_polys, name);
}

// The high poly objects: the ones listed in the settings, and with match by name the ones ending in _high
static bool util_bake_settings_high_poly(bake_settings_t *s, char *name) {
	return (s->match_by_name && util_bake_ends_with(name, "_high")) || util_bake_list_has(s->high_polys, name);
}

bool util_bake_is_high_poly(char *name) {
	return util_bake_settings_high_poly(&util_bake_settings, name);
}

// Lists the object as high poly or takes it out of the list
void util_bake_set_high_poly(char *name, bool high) {
	bake_settings_t *s = &util_bake_settings;
	if (util_bake_list_has(s->high_polys, name) == high) {
		return;
	}
	char *list = "";
	for (char *line = s->high_polys; line != NULL && *line != '\0';) {
		char *end  = strchr(line, '\n');
		char *item = end != NULL ? substring(line, 0, (i32)(end - line)) : line;
		if (!string_equals(item, name)) {
			list = list[0] == '\0' ? string_copy(item) : string("%s\n%s", list, item);
		}
		line = end != NULL ? end + 1 : NULL;
	}
	if (high) {
		list = list[0] == '\0' ? string_copy(name) : string("%s\n%s", list, name);
	}
	s->high_polys = list; // Jobs in the queue keep the list they were given
}

// A renamed object stays in the high poly list
void util_bake_rename_object(char *old_name, char *new_name) {
	if (old_name != NULL && new_name != NULL && util_bake_list_has(util_bake_settings.high_polys, old_name)) {
		util_bake_set_high_poly(old_name, false);
		util_bake_set_high_poly(new_name, true);
	}
}

// Length of the name without the _low or _high suffix, objects that share it are matched by name
static i32 util_bake_base_length(char *name) {
	i32 n = string_length(name);
	return util_bake_ends_with(name, "_high") ? n - 5 : util_bake_ends_with(name, "_low") ? n - 4 : n;
}

typedef struct util_bake_corner {
	i32 object;
	i32 q[3];  // Quantized position
	i32 index; // Triangle * 3 + vertex
} util_bake_corner_t;

static int util_bake_corner_compare(const void *a, const void *b) {
	const util_bake_corner_t *x = a;
	const util_bake_corner_t *y = b;
	if (x->object != y->object) {
		return x->object < y->object ? -1 : 1;
	}
	for (i32 k = 0; k < 3; ++k) {
		if (x->q[k] != y->q[k]) {
			return x->q[k] < y->q[k] ? -1 : 1;
		}
	}
	return 0;
}

// Welds the corners of the triangles: corners of one object at the same place, or at the same uv with by_uv, share
// an id. Returns the id of each corner (count * 3, free() it) and the number of ids in welds.
static i32 *util_bake_weld(util_bake_mesh_t *m, bool by_uv, f32 step, i32 *welds) {
	i32                 n       = m->count * 3;
	util_bake_corner_t *corners = malloc(sizeof(util_bake_corner_t) * (n > 0 ? n : 1));
	i32                *ids     = malloc(sizeof(i32) * (n > 0 ? n : 1));
	for (i32 i = 0; i < n; ++i) {
		f32 *p     = by_uv ? m->uv + i * 2 : m->pos + i * 3;
		corners[i] = (util_bake_corner_t){.object = m->object[i / 3],
		                                  .q      = {(i32)lroundf(p[0] / step), (i32)lroundf(p[1] / step), by_uv ? 0 : (i32)lroundf(p[2] / step)},
		                                  .index  = i};
	}
	qsort(corners, n, sizeof(util_bake_corner_t), util_bake_corner_compare);
	i32 count = 0;
	for (i32 i = 0; i < n; ++i) {
		if (i > 0 && util_bake_corner_compare(&corners[i - 1], &corners[i]) != 0) {
			count++;
		}
		ids[corners[i].index] = count;
	}
	*welds = n > 0 ? count + 1 : 0;
	free(corners);
	return ids;
}

// Ray directions: the normals of all vertices at the same place in one object averaged, so the rays leave no gap
// at hard edges where the vertex normals split
static void util_bake_average_normals(util_bake_mesh_t *m, f32 size) {
	i32  welds;
	i32 *ids = util_bake_weld(m, false, size * 1e-5f, &welds);
	f32 *sum = calloc((size_t)(welds > 0 ? welds : 1) * 3, sizeof(f32));
	for (i32 i = 0; i < m->count * 3; ++i) {
		for (i32 k = 0; k < 3; ++k) {
			sum[ids[i] * 3 + k] += m->nor[i * 3 + k];
		}
	}
	for (i32 i = 0; i < m->count * 3; ++i) {
		f32 *s   = sum + ids[i] * 3;
		f32  len = sqrtf(s[0] * s[0] + s[1] * s[1] + s[2] * s[2]);
		for (i32 k = 0; k < 3; ++k) {
			m->ray[i * 3 + k] = len > 1e-6f ? s[k] / len : m->nor[i * 3 + k];
		}
	}
	free(sum);
	free(ids);
}

static i32 util_bake_find(i32 *parent, i32 i) {
	while (parent[i] != i) {
		parent[i] = parent[parent[i]];
		i         = parent[i];
	}
	return i;
}

// UV islands: triangles of one object joined by a shared uv, numbered from 0
static void util_bake_islands(util_bake_mesh_t *m) {
	i32  welds;
	i32 *ids    = util_bake_weld(m, true, 1e-3f, &welds); // Uvs are in texels
	i32 *first  = malloc(sizeof(i32) * (welds > 0 ? welds : 1));
	i32 *parent = malloc(sizeof(i32) * (m->count > 0 ? m->count : 1));
	for (i32 i = 0; i < welds; ++i) {
		first[i] = -1;
	}
	for (i32 t = 0; t < m->count; ++t) {
		parent[t] = t;
	}
	for (i32 i = 0; i < m->count * 3; ++i) {
		i32 t = i / 3;
		i32 w = ids[i];
		if (first[w] < 0) {
			first[w] = t;
		}
		else {
			parent[util_bake_find(parent, t)] = util_bake_find(parent, first[w]);
		}
	}
	// Roots are numbered 0, 1, 2... in triangle order
	m->island   = malloc(sizeof(i32) * (m->count > 0 ? m->count : 1));
	i32 *number = malloc(sizeof(i32) * (m->count > 0 ? m->count : 1));
	i32  count  = 0;
	for (i32 t = 0; t < m->count; ++t) {
		number[t] = -1;
	}
	for (i32 t = 0; t < m->count; ++t) {
		i32 root = util_bake_find(parent, t);
		if (number[root] < 0) {
			number[root] = count++;
		}
		m->island[t] = number[root];
	}
	free(number);
	free(ids);
	free(first);
	free(parent);
}

// Curvature of each vertex from its neighbours along the edges, normals averaged over the vertices at the same place:
// convex is positive. Unlike the normals of a texel and its neighbours, it sees the edges across uv seams.
static void util_bake_vertex_curvature(util_bake_mesh_t *m, f32 size) {
	i32  welds;
	i32 *ids   = util_bake_weld(m, false, size * 1e-5f, &welds);
	i32  wc    = welds > 0 ? welds : 1;
	f32 *pos   = calloc((size_t)wc * 3, sizeof(f32));
	f32 *nor   = calloc((size_t)wc * 3, sizeof(f32));
	f32 *sum   = calloc((size_t)wc, sizeof(f32));
	i32 *count = calloc((size_t)wc, sizeof(i32));
	for (i32 i = 0; i < m->count * 3; ++i) {
		for (i32 k = 0; k < 3; ++k) {
			pos[ids[i] * 3 + k] = m->pos[i * 3 + k];
			nor[ids[i] * 3 + k] += m->nor[i * 3 + k];
		}
	}
	for (i32 w = 0; w < welds; ++w) {
		f32 *n   = nor + w * 3;
		f32  len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
		if (len > 1e-6f) {
			n[0] /= len;
			n[1] /= len;
			n[2] /= len;
		}
	}
	for (i32 t = 0; t < m->count; ++t) {
		for (i32 k = 0; k < 3; ++k) {
			i32  a     = ids[t * 3 + k];
			i32  b     = ids[t * 3 + (k + 1) % 3];
			f32 *pa    = pos + a * 3;
			f32 *pb    = pos + b * 3;
			f32 *na    = nor + a * 3;
			f32 *nb    = nor + b * 3;
			f32  dp[3] = {pb[0] - pa[0], pb[1] - pa[1], pb[2] - pa[2]};
			f32  len2  = dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2];
			if (a == b || len2 < 1e-20f) {
				continue;
			}
			f32 c = ((nb[0] - na[0]) * dp[0] + (nb[1] - na[1]) * dp[1] + (nb[2] - na[2]) * dp[2]) / len2;
			sum[a] += c;
			sum[b] += c;
			count[a]++;
			count[b]++;
		}
	}
	m->curv = malloc(sizeof(f32) * 3 * (m->count > 0 ? m->count : 1));
	for (i32 i = 0; i < m->count * 3; ++i) {
		m->curv[i] = count[ids[i]] > 0 ? sum[ids[i]] / count[ids[i]] : 0.0f;
	}
	free(pos);
	free(nor);
	free(sum);
	free(count);
	free(ids);
}

// Triangles of the objects in world space, uvs in texels of a res sized map
static void util_bake_mesh_build(bake_settings_t *s, util_bake_mesh_t *m, bool high, i32 res, bool colors, i32 *groups, util_bake_raster_t *r) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	i32                    count   = 0;
	for (i32 oi = 0; oi < objects->length; ++oi) {
		mesh_object_t *o = objects->buffer[oi];
		if (util_bake_settings_high_poly(s, o->base->name) == high) { // The settings of the job, the list may have changed since
			count += o->data->index_array->length / 3;
		}
	}
	m->count  = 0;
	m->pos    = malloc(sizeof(f32) * 9 * (count > 0 ? count : 1));
	m->nor    = malloc(sizeof(f32) * 9 * (count > 0 ? count : 1));
	m->ray    = malloc(sizeof(f32) * 9 * (count > 0 ? count : 1));
	m->uv     = malloc(sizeof(f32) * 6 * (count > 0 ? count : 1));
	m->object = malloc(sizeof(i32) * (count > 0 ? count : 1));
	m->group  = malloc(sizeof(i32) * (count > 0 ? count : 1));
	m->col    = colors ? malloc(12 * (count > 0 ? count : 1)) : NULL;

	for (i32 oi = 0; oi < objects->length; ++oi) {
		mesh_object_t *o = objects->buffer[oi];
		if (util_bake_settings_high_poly(s, o->base->name) != high) {
			continue;
		}
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
		mat4_t       w   = o->base->transform->world_unpack;
		mat4_t       wn  = o->base->transform->world;

		for (i32 t = 0; t + 2 < ind->length; t += 3) {
			i32 ti = m->count++;
			for (i32 k = 0; k < 3; ++k) {
				i32    i = ind->buffer[t + k];
				vec4_t v = util_bake_vertex(pa, i, 4, 3);
				v.w      = 1.0;
				v        = vec4_apply_mat4(v, w);
				vec4_t n = util_bake_vertex(na, i, 2, 2);
				n.z      = pa[i * 4 + 3] / 32767.0f;
				n.w      = 0.0;
				n        = vec4_norm(vec4_apply_mat4(n, wn));
				f32 *p   = m->pos + ti * 9 + k * 3;
				f32 *nn  = m->nor + ti * 9 + k * 3;
				p[0]     = v.x;
				p[1]     = v.y;
				p[2]     = v.z;
				nn[0]    = n.x;
				nn[1]    = n.y;
				nn[2]    = n.z;
				memcpy(m->ray + ti * 9 + k * 3, nn, sizeof(f32) * 3);
				m->uv[ti * 6 + k * 2]     = ta != NULL ? ta[i * 2] / 32767.0f * md->scale_tex * res : 0.0f;
				m->uv[ti * 6 + k * 2 + 1] = ta != NULL ? ta[i * 2 + 1] / 32767.0f * md->scale_tex * res : 0.0f;
				if (m->col != NULL) {
					for (i32 c = 0; c < 3; ++c) {
						f32 cv                      = ca != NULL ? ca[i * 4 + c] / 32767.0f : 1.0f;
						cv                          = cv < 0.0f ? 0.0f : cv > 1.0f ? 1.0f : cv;
						m->col[ti * 12 + k * 4 + c] = (u8)(cv * 255.0f + 0.5f);
					}
					m->col[ti * 12 + k * 4 + 3] = 255;
				}
				for (i32 a = 0; a < 3; ++a) {
					r->min[a] = fminf(r->min[a], p[a]);
					r->max[a] = fmaxf(r->max[a], p[a]);
				}
			}
			m->object[ti] = oi;
			m->group[ti]  = groups[oi];
		}
	}
}

// Skew and cage maps: paint layers with a bake role, left out of the material and read by the baker

static bool util_bake_roles_dirty = true;
static u32  util_bake_roles_hash  = 0;

slot_layer_t *util_bake_role_layer(i32 role) {
	slot_layer_t_array_t *layers = g_project->_->layers;
	for (i32 i = 0; layers != NULL && i < layers->length; ++i) {
		if (layers->buffer[i]->bake_role == role) {
			return layers->buffer[i];
		}
	}
	return NULL;
}

// A role layer was painted, filled, made or removed, or a history step may have done so
void util_bake_roles_changed() {
	util_bake_roles_dirty = true;
}

// Rgba floats of a texel of pixels read back from a texture
static void util_bake_texel_rgba(buffer_t *b, gpu_texture_t *t, i32 i, f32 *c) {
	if (t->format == GPU_TEXTURE_FORMAT_RGBA128) {
		memcpy(c, (f32 *)b->buffer + i * 4, sizeof(f32) * 4);
	}
	else if (t->format == GPU_TEXTURE_FORMAT_RGBA64) {
		u16 *h = (u16 *)b->buffer + i * 4;
		for (i32 k = 0; k < 4; ++k) {
			c[k] = render_path_raytrace_half_to_float(h[k]);
		}
	}
	else {
		for (i32 k = 0; k < 4; ++k) {
			c[k] = b->buffer[i * 4 + k] / 255.0f;
		}
	}
}

// Value of a role layer per texel: the gray painted times its alpha for a skew map, from -1 to 1 around a middle gray for a cage map
static f32 *util_bake_role_values(i32 role, i32 *w, i32 *h) {
	slot_layer_t *l = util_bake_role_layer(role);
	if (l == NULL || l->texpaint == NULL) {
		return NULL;
	}
	gpu_texture_t *t = l->texpaint;
	buffer_t      *b = gpu_get_texture_pixels(t);
	i32            n = t->width * t->height;
	f32           *v = malloc(sizeof(f32) * (n > 0 ? n : 1));
	for (i32 i = 0; i < n; ++i) {
		f32 c[4];
		util_bake_texel_rgba(b, t, i, c);
		f32 gray = (c[0] + c[1] + c[2]) / 3.0f; // Any channel order
		v[i]     = role == BAKE_ROLE_SKEW ? gray * c[3] : c[3] * (gray * 2.0f - 1.0f);
	}
	*w = t->width;
	*h = t->height;
	return v;
}

static f32 util_bake_role_sample(util_bake_raster_t *r, f32 *values, f32 px, f32 py) {
	i32 x = (i32)(px / r->res * r->role_w);
	i32 y = (i32)(py / r->res * r->role_h);
	x     = x < 0 ? 0 : x >= r->role_w ? r->role_w - 1 : x;
	y     = y < 0 ? 0 : y >= r->role_h ? r->role_h - 1 : y;
	return values[y * r->role_w + x];
}

typedef struct util_bake_edge {
	i32 a; // Weld ids of the corners, a < b
	i32 b;
	i32 tri;
	i32 k; // Edge from corner k to corner k + 1
} util_bake_edge_t;

static int util_bake_edge_compare(const void *x, const void *y) {
	const util_bake_edge_t *e = x;
	const util_bake_edge_t *f = y;
	return e->a != f->a ? (e->a < f->a ? -1 : 1) : e->b != f->b ? (e->b < f->b ? -1 : 1) : 0;
}

static void util_bake_face_normal(f32 *t, f32 *n) {
	f32 e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
	f32 e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
	n[0]      = e1[1] * e2[2] - e1[2] * e2[1];
	n[1]      = e1[2] * e2[0] - e1[0] * e2[2];
	n[2]      = e1[0] * e2[1] - e1[1] * e2[0];
	f32 len   = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
	if (len > 1e-20f) {
		n[0] /= len;
		n[1] /= len;
		n[2] /= len;
	}
}

// Bit k per triangle when its edge from corner k to k + 1 is a border, or joins a face bent by more than 30 degrees
static u8 *util_bake_hard_edges(util_bake_mesh_t *m, f32 size) {
	i32               welds;
	i32              *ids   = util_bake_weld(m, false, size * 1e-5f, &welds);
	util_bake_edge_t *edges = malloc(sizeof(util_bake_edge_t) * (m->count > 0 ? m->count * 3 : 1));
	u8               *hard  = calloc(m->count > 0 ? m->count : 1, 1);
	i32               n     = 0;
	for (i32 t = 0; t < m->count; ++t) {
		for (i32 k = 0; k < 3; ++k) {
			i32 a = ids[t * 3 + k];
			i32 b = ids[t * 3 + (k + 1) % 3];
			if (a != b) {
				edges[n++] = (util_bake_edge_t){.a = a < b ? a : b, .b = a < b ? b : a, .tri = t, .k = k};
			}
		}
	}
	qsort(edges, n, sizeof(util_bake_edge_t), util_bake_edge_compare);
	for (i32 i = 0; i < n;) {
		i32 j = i + 1;
		while (j < n && util_bake_edge_compare(&edges[i], &edges[j]) == 0) {
			j++;
		}
		bool smooth = false;
		if (j - i == 2) {
			f32 n0[3];
			f32 n1[3];
			util_bake_face_normal(m->pos + edges[i].tri * 9, n0);
			util_bake_face_normal(m->pos + edges[i + 1].tri * 9, n1);
			smooth = n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2] > 0.866f;
		}
		for (i32 e = i; e < j && !smooth; ++e) {
			hard[edges[e].tri] |= 1 << edges[e].k;
		}
		i = j;
	}
	free(edges);
	free(ids);
	return hard;
}

// One sample of the low poly per texel center: the triangle that covers it
static void util_bake_raster_low(util_bake_raster_t *r) {
	i32               res = r->res;
	util_bake_mesh_t *m   = &r->low;
	for (i32 t = 0; t < m->count; ++t) {
		f32 *uv   = m->uv + t * 6;
		f32  x[3] = {uv[0], uv[2], uv[4]};
		f32  y[3] = {uv[1], uv[3], uv[5]};
		f32  area = (x[1] - x[0]) * (y[2] - y[0]) - (x[2] - x[0]) * (y[1] - y[0]);
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
				if (b0 >= -1e-4f && b1 >= -1e-4f && b2 >= -1e-4f) {
					r->tri[py * res + px] = t;
				}
			}
		}
	}
}

// Low poly triangles in uv space, and the high poly or the low poly in a bvh when rays are cast
static util_bake_raster_t *util_bake_raster_create(bake_settings_t *s, i32 res, bool colors, bool rays, bool vertex_curvature) {
	util_bake_raster_t *r = calloc(1, sizeof(util_bake_raster_t));
	r->res                = res;
	r->tri                = malloc((size_t)res * res * sizeof(i32));
	for (i32 i = 0; i < res * res; ++i) {
		r->tri[i] = -1;
	}
	for (i32 a = 0; a < 3; ++a) {
		r->min[a] = FLT_MAX;
		r->max[a] = -FLT_MAX;
	}

	// Match by name: objects that share the name without _low and _high share a group
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	i32                   *groups  = malloc(sizeof(i32) * (objects->length > 0 ? objects->length : 1));
	for (i32 i = 0; i < objects->length; ++i) {
		groups[i]  = i;
		char *name = objects->buffer[i]->base->name;
		i32   n    = util_bake_base_length(name);
		for (i32 j = 0; j < i; ++j) {
			char *other = objects->buffer[j]->base->name;
			if (util_bake_base_length(other) == n && strncmp(other, name, n) == 0) {
				groups[i] = groups[j];
				break;
			}
		}
	}
	util_bake_mesh_build(s, &r->low, false, res, colors, groups, r);
	util_bake_mesh_build(s, &r->high, true, res, colors, groups, r);
	free(groups);

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
	if (s->average_normals) {
		util_bake_average_normals(&r->low, r->size);
	}
	util_bake_islands(&r->low);
	if (vertex_curvature) {
		util_bake_vertex_curvature(&r->low, r->size);
		util_bake_vertex_curvature(&r->high, r->size);
	}
	util_bake_raster_low(r);

	if (r->high.count > 0) { // Skew and cage maps only change the rays to the high poly
		r->skew = util_bake_role_values(BAKE_ROLE_SKEW, &r->role_w, &r->role_h);
		r->cage = util_bake_role_values(BAKE_ROLE_CAGE, &r->role_w, &r->role_h);
		if (s->edge_protection && (r->skew != NULL || s->skew_base == BAKE_SKEW_BASE_TRIANGLE)) {
			r->hard = util_bake_hard_edges(&r->low, r->size);
		}
	}

	// The high poly is cast at for every slot, the low poly only for occlusion, bent normals and thickness
	util_bake_mesh_t *traced = r->high.count > 0 ? &r->high : rays ? &r->low : NULL;
	if (traced != NULL) {
		f32 *tris = malloc(sizeof(f32) * 9 * (traced->count > 0 ? traced->count : 1));
		i32 *ids  = malloc(sizeof(i32) * (traced->count > 0 ? traced->count : 1));
		memcpy(tris, traced->pos, sizeof(f32) * 9 * traced->count);
		for (i32 i = 0; i < traced->count; ++i) {
			ids[i] = i;
		}
		util_bvh_build(&r->bvh, tris, ids, traced->count);
	}
	return r;
}

// Barycentrics of a point of the uv space in a triangle, outside of it they extrapolate
static void util_bake_barycentrics(f32 *uv, f32 px, f32 py, f32 *b) {
	f32 area = (uv[2] - uv[0]) * (uv[5] - uv[1]) - (uv[4] - uv[0]) * (uv[3] - uv[1]);
	b[0]     = ((uv[2] - px) * (uv[5] - py) - (uv[4] - px) * (uv[3] - py)) / area;
	b[1]     = ((uv[4] - px) * (uv[1] - py) - (uv[0] - px) * (uv[5] - py)) / area;
	b[2]     = 1.0f - b[0] - b[1];
}

static void util_bake_interpolate(f32 *v, f32 *b, f32 *out, bool normalize) {
	for (i32 k = 0; k < 3; ++k) {
		out[k] = v[k] * b[0] + v[3 + k] * b[1] + v[6 + k] * b[2];
	}
	if (normalize) {
		f32 len = sqrtf(out[0] * out[0] + out[1] * out[1] + out[2] * out[2]);
		if (len > 1e-12f) {
			out[0] /= len;
			out[1] /= len;
			out[2] /= len;
		}
	}
}

// Turns the ray direction from the smooth normals toward the triangle normal by the skew map
static void util_bake_skew(bake_job_t *job, i32 tri, f32 *b, f32 px, f32 py, f32 *ln, f32 *rd) {
	util_bake_raster_t *r = job->raster;
	f32                 fn[3];
	util_bake_face_normal(r->low.pos + tri * 9, fn);
	if (fn[0] * ln[0] + fn[1] * ln[1] + fn[2] * ln[2] < 0.0f) {
		fn[0] = -fn[0];
		fn[1] = -fn[1];
		fn[2] = -fn[2];
	}
	f32 k = r->skew != NULL ? util_bake_role_sample(r, r->skew, px, py) : 0.0f;
	k     = k < 0.0f ? 0.0f : k > 1.0f ? 1.0f : k;
	k     = job->settings.skew_base == BAKE_SKEW_BASE_TRIANGLE ? 1.0f - k : k; // Weight of the triangle normal
	if (r->hard != NULL) { // Back to the smooth normals over the quarter of the triangle next to a hard edge
		for (i32 e = 0; e < 3; ++e) {
			if ((r->hard[tri] >> e) & 1) {
				f32 d = b[(e + 2) % 3] / 0.25f; // 0 on the edge
				d     = d < 0.0f ? 0.0f : d > 1.0f ? 1.0f : d;
				k *= d * d * (3.0f - 2.0f * d);
			}
		}
	}
	f32 len = 0.0f;
	for (i32 c = 0; c < 3; ++c) {
		rd[c] = rd[c] * (1.0f - k) + fn[c] * k;
		len += rd[c] * rd[c];
	}
	len = sqrtf(len);
	if (len > 1e-12f) {
		rd[0] /= len;
		rd[1] /= len;
		rd[2] /= len;
	}
	else {
		memcpy(rd, fn, sizeof(f32) * 3);
	}
}

// True when the ray met the high poly triangle from behind, going the way its normal points
static bool util_bake_hit_back(util_bake_raster_t *r, i32 tri, f32 *d) {
	f32 *t     = r->high.pos + tri * 9;
	f32  e1[3] = {t[3] - t[0], t[4] - t[1], t[5] - t[2]};
	f32  e2[3] = {t[6] - t[0], t[7] - t[1], t[8] - t[2]};
	f32  n[3]  = {e1[1] * e2[2] - e1[2] * e2[1], e1[2] * e2[0] - e1[0] * e2[2], e1[0] * e2[1] - e1[1] * e2[0]};
	return n[0] * d[0] + n[1] * d[1] + n[2] * d[2] > 0.0f;
}

// Height of the cage above a low poly point: just past the high poly surface above when the point lies inside the high poly
// (the nearest surface above is met from behind, before any surface below is met from the front), else on the low poly
static f32 util_bake_auto_cage(util_bake_raster_t *r, f32 *p, f32 *rd, f32 max_front, f32 rear, i32 group) {
	f32       margin  = r->size * 1e-3f;
	f32       up[3]   = {rd[0], rd[1], rd[2]};
	f32       down[3] = {-rd[0], -rd[1], -rd[2]};
	bvh_hit_t above;
	bvh_hit_t below;
	if (!util_bvh_raycast(&r->bvh, p, up, max_front, false, false, r->high.group, group, &above) || !util_bake_hit_back(r, above.tri, up)) {
		return margin;
	}
	if (util_bvh_raycast(&r->bvh, p, down, rear, false, false, r->high.group, group, &below) && !util_bake_hit_back(r, below.tri, down) && below.t < above.t) {
		return margin;
	}
	return above.t + margin;
}

// What a sample of the map sees: the low poly at a point of its uv space, or the high poly found by a ray from there
static void util_bake_surface(bake_job_t *job, i32 tri, f32 px, f32 py, util_bake_surface_t *s) {
	util_bake_raster_t *r   = job->raster;
	util_bake_mesh_t   *low = &r->low;
	f32                 b[3];
	util_bake_barycentrics(low->uv + tri * 6, px, py, b);
	util_bake_interpolate(low->pos + tri * 9, b, s->lp, false);
	util_bake_interpolate(low->nor + tri * 9, b, s->ln, true);
	s->ltri   = tri;
	s->high   = false;
	s->hit    = true;
	s->object = low->object[tri];
	s->tri    = tri;
	memcpy(s->b, b, sizeof(s->b));
	memcpy(s->p, s->lp, sizeof(s->p));
	memcpy(s->n, s->ln, sizeof(s->n));
	for (i32 k = 0; k < 3; ++k) {
		s->col[k] = low->col != NULL ? (low->col[tri * 12 + k] * b[0] + low->col[tri * 12 + 4 + k] * b[1] + low->col[tri * 12 + 8 + k] * b[2]) / 255.0f : 1.0f;
	}
	if (r->high.count == 0) {
		return;
	}

	// From the cage above the low poly down to the rear distance below it
	f32 rd[3];
	util_bake_interpolate(low->ray + tri * 9, b, rd, true);
	if (r->skew != NULL || job->settings.skew_base == BAKE_SKEW_BASE_TRIANGLE) {
		util_bake_skew(job, tri, b, px, py, s->ln, rd);
	}
	f32       front = job->settings.front_distance * r->size;
	f32       rear  = job->settings.rear_distance * r->size;
	f32       d[3]  = {-rd[0], -rd[1], -rd[2]};
	i32       group = job->settings.match_by_name ? low->group[tri] : -1;
	bvh_hit_t h;
	if (job->settings.cage == BAKE_CAGE_AUTO) {
		front = util_bake_auto_cage(r, s->lp, rd, front, rear, group);
	}
	if (r->cage != NULL) { // Painted offset over the cage
		front += util_bake_role_sample(r, r->cage, px, py) * job->settings.cage_range * r->size;
		front = front > 0.0f ? front : 0.0f;
	}
	f32 o[3] = {s->lp[0] + rd[0] * front, s->lp[1] + rd[1] * front, s->lp[2] + rd[2] * front};
	if (!util_bvh_raycast(&r->bvh, o, d, front + rear, false, job->settings.ignore_backface, r->high.group, group, &h)) {
		s->hit = false;
		return;
	}
	util_bake_mesh_t *high  = &r->high;
	f32               hb[3] = {1.0f - h.u - h.v, h.u, h.v};
	s->high                 = true;
	s->tri                  = h.tri;
	memcpy(s->b, hb, sizeof(s->b));
	s->object = high->object[h.tri];
	util_bake_interpolate(high->pos + h.tri * 9, hb, s->p, false);
	util_bake_interpolate(high->nor + h.tri * 9, hb, s->n, true);
	for (i32 k = 0; k < 3; ++k) {
		s->col[k] = high->col != NULL
		                ? (high->col[h.tri * 12 + k] * hb[0] + high->col[h.tri * 12 + 4 + k] * hb[1] + high->col[h.tri * 12 + 8 + k] * hb[2]) / 255.0f
		                : 1.0f;
	}
}

static f32 util_bake_det3(f32 *a, f32 *b, f32 *c) {
	return a[0] * (b[1] * c[2] - b[2] * c[1]) - b[0] * (a[1] * c[2] - a[2] * c[1]) + c[0] * (a[1] * b[2] - a[2] * b[1]);
}

// Tangent space normal, OpenGL convention: x along the gradient of u, y along the gradient of v going up the image,
// z along the low poly normal. The frame is the one of cotangent_frame in make_mesh.c: the gradients of u and v on
// the low poly triangle, scaled together by the longest one, and y flipped like the viewport does
static void util_bake_tangent_normal(util_bake_raster_t *r, util_bake_surface_t *s, f32 *out) {
	util_bake_mesh_t *low    = &r->low;
	f32              *p      = low->pos + s->ltri * 9;
	f32              *uv     = low->uv + s->ltri * 6;
	f32              *n      = s->ln;
	f32               e1[3]  = {p[3] - p[0], p[4] - p[1], p[5] - p[2]};
	f32               e2[3]  = {p[6] - p[0], p[7] - p[1], p[8] - p[2]};
	f32               du1    = uv[2] - uv[0];
	f32               dv1    = uv[3] - uv[1];
	f32               du2    = uv[4] - uv[0];
	f32               dv2    = uv[5] - uv[1];
	f32               e2n[3] = {e2[1] * n[2] - e2[2] * n[1], e2[2] * n[0] - e2[0] * n[2], e2[0] * n[1] - e2[1] * n[0]};
	f32               ne1[3] = {n[1] * e1[2] - n[2] * e1[1], n[2] * e1[0] - n[0] * e1[2], n[0] * e1[1] - n[1] * e1[0]};
	f32               det    = n[0] * (e1[1] * e2[2] - e1[2] * e2[1]) + n[1] * (e1[2] * e2[0] - e1[0] * e2[2]) + n[2] * (e1[0] * e2[1] - e1[1] * e2[0]);
	f32               t[3];
	f32               bt[3];
	for (i32 k = 0; k < 3; ++k) {
		t[k]  = (e2n[k] * du1 + ne1[k] * du2) * (det < 0.0f ? -1.0f : 1.0f);
		bt[k] = (e2n[k] * dv1 + ne1[k] * dv2) * (det < 0.0f ? -1.0f : 1.0f);
	}
	f32 tl = t[0] * t[0] + t[1] * t[1] + t[2] * t[2];
	f32 bl = bt[0] * bt[0] + bt[1] * bt[1] + bt[2] * bt[2];
	f32 m  = sqrtf(tl > bl ? tl : bl);
	if (m < 1e-12f) {
		out[0] = 0.5;
		out[1] = 0.5;
		out[2] = 1.0;
		return;
	}
	f32 by[3] = {-bt[0] / m, -bt[1] / m, -bt[2] / m}; // The viewport flips y
	for (i32 k = 0; k < 3; ++k) {
		t[k] /= m;
	}
	// Solve t x + by y + n z = high normal
	f32 d = util_bake_det3(t, by, n);
	if (fabsf(d) < 1e-9f) {
		out[0] = 0.5;
		out[1] = 0.5;
		out[2] = 1.0;
		return;
	}
	f32 v[3] = {util_bake_det3(s->n, by, n) / d, util_bake_det3(t, s->n, n) / d, util_bake_det3(t, by, s->n) / d};
	f32 len  = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
	for (i32 k = 0; k < 3; ++k) {
		out[k] = (len > 1e-12f ? v[k] / len : (k == 2 ? 1.0f : 0.0f)) * 0.5f + 0.5f;
	}
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

// Change of the normal along the surface between the texel and its neighbours of the same object
static f32 util_bake_curvature(bake_job_t *job, i32 x, i32 y) {
	util_bake_raster_t *r   = job->raster;
	i32                 res = r->res;
	util_bake_surface_t a;
	util_bake_surface(job, r->tri[y * res + x], x + 0.5f, y + 0.5f, &a);
	if (!a.hit) {
		return 0.5;
	}
	f32 sum   = 0.0;
	i32 count = 0;
	i32 nx[4] = {x - 1, x + 1, x, x};
	i32 ny[4] = {y, y, y - 1, y + 1};
	for (i32 k = 0; k < 4; ++k) {
		if (nx[k] < 0 || ny[k] < 0 || nx[k] >= res || ny[k] >= res) {
			continue;
		}
		i32 tj = r->tri[ny[k] * res + nx[k]];
		if (tj < 0) {
			continue;
		}
		util_bake_surface_t b;
		util_bake_surface(job, tj, nx[k] + 0.5f, ny[k] + 0.5f, &b);
		if (!b.hit || b.object != a.object || b.high != a.high) {
			continue;
		}
		f32 dp[3] = {b.p[0] - a.p[0], b.p[1] - a.p[1], b.p[2] - a.p[2]};
		f32 len2  = dp[0] * dp[0] + dp[1] * dp[1] + dp[2] * dp[2];
		if (len2 < 1e-20f) {
			continue;
		}
		// Normals that spread apart along the surface are convex, positive
		sum += ((b.n[0] - a.n[0]) * dp[0] + (b.n[1] - a.n[1]) * dp[1] + (b.n[2] - a.n[2]) * dp[2]) / len2;
		count++;
	}
	if (count == 0) {
		return 0.5;
	}
	f32 k = sum / count * r->size * job->settings.curvature_strength * 0.1f;
	k     = k < -1.0f ? -1.0f : k > 1.0f ? 1.0f : k;
	return 0.5f + 0.5f * k;
}

// One sample of a slot, seed spreads the ray patterns of the samples of a texel
static void util_bake_sample(bake_job_t *job, util_bake_surface_t *s, u32 seed, i32 rays, f32 *out) {
	util_bake_raster_t *r = job->raster;
	f32                *p = s->p;
	f32                *n = s->n;
	out[3]                = 1.0;

	switch (job->slot) {
	case MESH_MAP_NORMAL:
		if (s->high) {
			util_bake_tangent_normal(r, s, out);
		}
		else { // Flat without a high poly under the sample
			out[0] = 0.5;
			out[1] = 0.5;
			out[2] = 1.0;
		}
		break;
	case MESH_MAP_WORLD_NORMAL:
		for (i32 k = 0; k < 3; ++k) {
			out[k] = n[k] * 0.5f + 0.5f;
		}
		break;
	case MESH_MAP_OCCLUSION:
	case MESH_MAP_BENT_NORMAL: { // Unoccluded directions of the occlusion rays average to the bent normal
		f32 dist    = job->settings.occlusion_distance * r->size;
		f32 eps     = r->size * 2e-4f;
		f32 o[3]    = {p[0] + n[0] * eps, p[1] + n[1] * eps, p[2] + n[2] * eps};
		f32 r1      = util_bake_hash(seed * 2 + 1);
		f32 r2      = util_bake_hash(seed * 2 + 2);
		i32 hits    = 0;
		f32 bent[3] = {0.0, 0.0, 0.0};
		for (i32 i = 0; i < rays; ++i) {
			f32       d[3];
			bvh_hit_t hit;
			util_bake_hemisphere(n, i, rays, r1, r2, d);
			if (util_bvh_raycast(&r->bvh, o, d, dist, true, false, NULL, -1, &hit)) {
				hits++;
			}
			else {
				bent[0] += d[0];
				bent[1] += d[1];
				bent[2] += d[2];
			}
		}
		if (job->slot == MESH_MAP_OCCLUSION) {
			out[0] = out[1] = out[2] = 1.0f - (f32)hits / rays;
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
		f32 dist  = job->settings.thickness_distance * r->size;
		f32 eps   = r->size * 2e-4f;
		f32 in[3] = {-n[0], -n[1], -n[2]};
		f32 o[3]  = {p[0] + in[0] * eps, p[1] + in[1] * eps, p[2] + in[2] * eps};
		f32 r1    = util_bake_hash(seed * 2 + 1);
		f32 r2    = util_bake_hash(seed * 2 + 2);
		f32 sum   = 0.0;
		for (i32 i = 0; i < rays; ++i) {
			f32       d[3];
			bvh_hit_t hit;
			util_bake_hemisphere(in, i, rays, r1, r2, d);
			sum += util_bvh_raycast(&r->bvh, o, d, dist, false, false, NULL, -1, &hit) ? hit.t : dist;
		}
		out[0] = out[1] = out[2] = sum / rays / dist; // White is thick
		break;
	}
	case MESH_MAP_ID:
		if (job->settings.id_source == MESH_MAP_ID_VERTEX_COLOR) {
			memcpy(out, s->col, sizeof(f32) * 3);
		}
		else if (job->settings.id_source == MESH_MAP_ID_UV_ISLAND) {
			util_bake_id_color(r->low.island[s->ltri], out);
		}
		else {
			util_bake_id_color(s->object, out);
		}
		break;
	case MESH_MAP_POSITION:
		for (i32 k = 0; k < 3; ++k) {
			f32 ext = r->max[k] - r->min[k];
			out[k]  = ext > 1e-6f ? (p[k] - r->min[k]) / ext : 0.5f;
		}
		break;
	case MESH_MAP_OPACITY:
		out[0] = out[1] = out[2] = s->hit ? 1.0f : 0.0f;
		break;
	case MESH_MAP_CURVATURE: { // Mesh mode, from the curvature of the vertices
		f32 *c = (s->high ? r->high.curv : r->low.curv) + s->tri * 3;
		f32  k = (c[0] * s->b[0] + c[1] * s->b[1] + c[2] * s->b[2]) * r->size * job->settings.curvature_strength * 0.1f;
		k      = k < -1.0f ? -1.0f : k > 1.0f ? 1.0f : k;
		out[0] = out[1] = out[2] = 0.5f + 0.5f * k;
		break;
	}
	}
}

static void util_bake_texel(bake_job_t *job, i32 x, i32 y, f32 *out) {
	util_bake_raster_t *r   = job->raster;
	i32                 ti  = y * r->res + x;
	i32                 tri = r->tri[ti];
	if (job->slot == MESH_MAP_CURVATURE && job->settings.curvature_mode == MESH_MAP_CURVATURE_NORMALS) { // From the neighbours, no antialiasing
		out[0] = out[1] = out[2] = util_bake_curvature(job, x, y);
		out[3]                   = 1.0;
		return;
	}

	// Antialiasing: aa x aa samples spread over the texel, the rays of a texel shared among them
	i32 aa      = job->settings.aa < 1 ? 1 : job->settings.aa > 8 ? 8 : job->settings.aa;
	i32 samples = aa * aa;
	i32 rays    = job->slot == MESH_MAP_THICKNESS ? job->settings.thickness_samples : job->settings.occlusion_samples;
	rays        = rays / samples > 0 ? rays / samples : 1;
	f32 sum[4]  = {0.0, 0.0, 0.0, 0.0};
	for (i32 j = 0; j < aa; ++j) {
		for (i32 i = 0; i < aa; ++i) {
			util_bake_surface_t s;
			f32                 v[4];
			util_bake_surface(job, tri, x + (i + 0.5f) / aa, y + (j + 0.5f) / aa, &s);
			if (!s.hit && job->slot != MESH_MAP_OPACITY) {
				s.hit = true; // The low poly stands in where the ray finds no high poly
			}
			util_bake_sample(job, &s, (u32)ti * 64 + j * aa + i, rays, v);
			for (i32 k = 0; k < 4; ++k) {
				sum[k] += v[k];
			}
		}
	}
	for (i32 k = 0; k < 4; ++k) {
		out[k] = sum[k] / samples;
	}
	if (util_bake_slot_is_vector(job->slot) && job->slot != MESH_MAP_POSITION) { // Average directions stay unit length
		f32 v[3] = {out[0] * 2.0f - 1.0f, out[1] * 2.0f - 1.0f, out[2] * 2.0f - 1.0f};
		f32 len  = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
		if (len > 1e-6f) {
			for (i32 k = 0; k < 3; ++k) {
				out[k] = v[k] / len * 0.5f + 0.5f;
			}
		}
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
			if (job->raster->tri[ti] >= 0) {
				util_bake_texel(job, x, y, job->out + ti * 4);
			}
		}
	}
}

static bool util_bake_slot_casts_rays(i32 slot) {
	return slot == MESH_MAP_OCCLUSION || slot == MESH_MAP_BENT_NORMAL || slot == MESH_MAP_THICKNESS;
}

// Grows the uv islands by copying the average of the filled neighbours, the rest gets the default value
static void util_bake_dilate(f32 *px, i32 *tri, i32 res, i32 dilation, i32 slot) {
	u8 *mask = malloc((size_t)res * res);
	u8 *next = malloc((size_t)res * res);
	for (i32 i = 0; i < res * res; ++i) {
		mask[i] = tri[i] >= 0;
	}
	for (i32 pass = 0; pass < dilation; ++pass) {
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
	util_bake_default_value(slot, def);
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
static gpu_texture_t *util_bake_uv_islands = NULL;

// A color per uv island of the objects that are not high poly, read by the UV Island type of the Bake Texture node
void util_bake_make_uv_islands(i32 res) {
	util_bake_raster_t *r  = util_bake_raster_create(&util_bake_settings, res, false, false, false);
	f32                *px = calloc((size_t)res * res * 4, sizeof(f32));
	for (i32 i = 0; i < res * res; ++i) {
		if (r->tri[i] >= 0) {
			util_bake_id_color(r->low.island[r->tri[i]], px + i * 4);
			px[i * 4 + 3] = 1.0;
		}
	}
	util_bake_dilate(px, r->tri, res, 2, MESH_MAP_ID);
	if (util_bake_uv_islands != NULL) {
		gpu_delete_texture(util_bake_uv_islands);
	}
	util_bake_uv_islands = util_bake_upload(MESH_MAP_ID, px, res);
	free(px);
	util_bake_raster_free(r);
}

gpu_texture_t *util_bake_uv_islands_texture() {
	return util_bake_uv_islands;
}

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

// Reads the role layers back when they changed, not while one is being painted. Called every frame.
static void util_bake_roles_update() {
	if (!util_bake_roles_dirty || (g_context->layer != NULL && g_context->layer->bake_role != BAKE_ROLE_NONE && g_context->pdirty > 0)) {
		return;
	}
	util_bake_roles_dirty  = false;
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}
	u32 h = 2166136261u;
	for (i32 role = BAKE_ROLE_SKEW; role <= BAKE_ROLE_CAGE; ++role) {
		slot_layer_t *l = util_bake_role_layer(role);
		h               = (h ^ (u32)(l != NULL)) * 16777619u;
		if (l == NULL || l->texpaint == NULL) {
			continue;
		}
		buffer_t *b    = gpu_get_texture_pixels(l->texpaint);
		i32       step = b->length / (UTIL_BAKE_HASH_SAMPLES * 64) > 1 ? b->length / (UTIL_BAKE_HASH_SAMPLES * 64) : 1;
		for (i32 i = 0; i < b->length; i += step) {
			h = (h ^ b->buffer[i]) * 16777619u;
		}
	}
	util_bake_roles_hash = h;
	if (in_use) {
		draw_begin(current, false, 0);
	}
}

// Selects the layer of a role to paint it, made on top of the stack the first time
slot_layer_t *util_bake_paint_role(i32 role) {
	if (role != BAKE_ROLE_SKEW && role != BAKE_ROLE_CAGE) {
		return NULL;
	}
	slot_layer_t *l = util_bake_role_layer(role);
	if (l == NULL) {
		base_init_undo_layers();
		l = layers_new_layer(false, g_project->_->layers->length, NULL);
		if (l == NULL) {
			return NULL;
		}
		history_new_layer();
		l->bake_role = role;
		l->name      = string_copy(role == BAKE_ROLE_SKEW ? tr("Skew Map") : tr("Cage Map"));
		slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		util_bake_roles_changed();
	}
	context_set_layer(l);
	context_update_role_view(); // A new layer was selected before it got its role
	make_material_parse_paint_material(true);
	ui_dock_redraw(TAB_AREA_SIDEBAR0);
	return l;
}

// Fills a role layer with a gray value, or clears it to transparent (no skew, no cage offset) when value is below 0
void util_bake_role_fill(i32 role, f32 value) {
	slot_layer_t *l = util_bake_role_layer(role);
	if (l == NULL) {
		return;
	}
	slot_layer_t *_layer = g_context->layer;
	g_context->layer     = l;
	history_clear_layer();
	g_context->layer = _layer;
	i32 c            = value < 0.0f ? 0 : (i32)((value > 1.0f ? 1.0f : value) * 255.0f + 0.5f);
	slot_layer_clear(l, value < 0.0f ? 0x00000000 : (i32)(0xff000000 | (c << 16) | (c << 8) | c), NULL, 1.0, layers_default_rough, 0.0);
	g_context->layers_preview_dirty = true;
	g_context->ddirty               = 2;
	util_bake_roles_changed();
	util_bake_roles_update();
}

static u32 util_bake_fnv(u32 h, void *data, i32 size) {
	u8 *b = data;
	for (i32 i = 0; i < size; ++i) {
		h = (h ^ b[i]) * 16777619u;
	}
	return h;
}

static u32 util_bake_fnv_i32(u32 h, i32 v) {
	return util_bake_fnv(h, &v, sizeof(v));
}

// Rounded, so a value that went through a text field or a saved project hashes the same
static u32 util_bake_fnv_f32(u32 h, f32 v) {
	v = v < -1000.0f ? -1000.0f : v > 1000.0f ? 1000.0f : v;
	return util_bake_fnv_i32(h, (i32)lroundf(v * 1e6f));
}

static u32 util_bake_fnv_string(u32 h, char *s) {
	s = s != NULL ? s : "";
	return util_bake_fnv(h, s, string_length(s) + 1);
}

// The settings that change what a slot bakes
static u32 util_bake_settings_hash(i32 slot, bake_settings_t *s) {
	u32 h = util_bake_fnv_i32(2166136261u, slot);
	h     = util_bake_fnv_i32(h, s->res);
	h     = util_bake_fnv_i32(h, s->dilation);
	if (!(slot == MESH_MAP_CURVATURE && s->curvature_mode == MESH_MAP_CURVATURE_NORMALS)) { // Curvature from the normals has no antialiasing
		h = util_bake_fnv_i32(h, s->aa);
	}
	h = util_bake_fnv_f32(h, s->front_distance);
	h = util_bake_fnv_f32(h, s->rear_distance);
	h = util_bake_fnv_i32(h, s->average_normals);
	h = util_bake_fnv_i32(h, s->ignore_backface);
	h = util_bake_fnv_i32(h, s->match_by_name);
	h = util_bake_fnv_i32(h, s->cage);
	h = util_bake_fnv_i32(h, s->skew_base);
	h = util_bake_fnv_i32(h, s->edge_protection);
	h = util_bake_fnv_f32(h, s->cage_range);
	h = util_bake_fnv_string(h, s->high_polys);
	if (slot == MESH_MAP_OCCLUSION || slot == MESH_MAP_BENT_NORMAL) {
		h = util_bake_fnv_i32(h, s->occlusion_samples);
		h = util_bake_fnv_f32(h, s->occlusion_distance);
	}
	else if (slot == MESH_MAP_THICKNESS) {
		h = util_bake_fnv_i32(h, s->thickness_samples);
		h = util_bake_fnv_f32(h, s->thickness_distance);
	}
	else if (slot == MESH_MAP_CURVATURE) {
		h = util_bake_fnv_f32(h, s->curvature_strength);
		h = util_bake_fnv_i32(h, s->curvature_mode);
	}
	else if (slot == MESH_MAP_ID) {
		h = util_bake_fnv_i32(h, s->id_source);
	}
	return h;
}

static bool util_bake_uses_colors(i32 slot, bake_settings_t *s) {
	return slot == MESH_MAP_ID && s->id_source == MESH_MAP_ID_VERTEX_COLOR;
}

// The objects as the bakers see them: names, transforms and a sample of each vertex array, and the skew and cage maps
// when there is a high poly
static u32 util_bake_scene_hash(bake_settings_t *s, bool colors) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	u32                    h       = util_bake_fnv_i32(2166136261u, objects != NULL ? objects->length : 0);
	for (i32 oi = 0; objects != NULL && oi < objects->length; ++oi) {
		if (util_bake_settings_high_poly(s, objects->buffer[oi]->base->name)) {
			h = util_bake_fnv_i32(h, util_bake_roles_hash);
			break;
		}
	}
	for (i32 oi = 0; objects != NULL && oi < objects->length; ++oi) {
		mesh_object_t *o  = objects->buffer[oi];
		mesh_data_t   *md = o->data;
		h                 = util_bake_fnv_string(h, o->base->name);
		f32 *w            = (f32 *)&o->base->transform->world_unpack;
		for (i32 i = 0; i < 16; ++i) { // Rounded: a loaded transform is rebuilt from its parts and can differ in the last bits
			h = util_bake_fnv_f32(h, roundf(w[i] * 1e4f) / 1e4f);
		}
		h                 = util_bake_fnv_f32(h, md->scale_tex);
		u32_array_t *ind  = md->index_array;
		i32          step = ind->length / UTIL_BAKE_HASH_SAMPLES > 1 ? ind->length / UTIL_BAKE_HASH_SAMPLES : 1;
		h                 = util_bake_fnv_i32(h, ind->length);
		for (i32 i = 0; i < ind->length; i += step) {
			h = util_bake_fnv_i32(h, ind->buffer[i]);
		}
		char *arrays[]     = {"pos", "nor", "tex", "col"};
		i32   components[] = {4, 2, 2, 4}; // Values per vertex
		for (i32 a = 0; a < (colors ? 4 : 3); ++a) {
			vertex_array_t *va = mesh_data_get_vertex_array(md, arrays[a]);
			if (va == NULL) {
				h = util_bake_fnv_i32(h, -1);
				continue;
			}
			i16_array_t *v     = va->values;
			i32          c     = components[a];
			i32          verts = v->length / c;
			step               = verts / UTIL_BAKE_HASH_SAMPLES > 1 ? verts / UTIL_BAKE_HASH_SAMPLES : 1;
			h                  = util_bake_fnv_i32(h, v->length);
			for (i32 i = 0; i < verts; i += step) { // Whole vertices, a step in values could skip the same component every time
				h = util_bake_fnv(h, &v->buffer[i * c], sizeof(i16) * c);
			}
		}
	}
	return h;
}

// Even and never 0, a quick preview stores it with the lowest bit set
static u32 util_bake_combine(u32 settings, u32 scene) {
	u32 h = util_bake_fnv_i32(util_bake_fnv_i32(2166136261u, settings), scene) & ~1u;
	return h != 0 ? h : 2;
}

// What a bake of the slot with the current settings and scene would be made from
u32 util_bake_signature(i32 slot) {
	bake_settings_t *s = &util_bake_settings;
	return util_bake_combine(util_bake_settings_hash(slot, s), util_bake_scene_hash(s, util_bake_uses_colors(slot, s)));
}

// Bit per slot holding a map baked from other settings or another scene, or only a quick preview
i32 util_bake_outdated_mask() {
	bake_settings_t *s    = &util_bake_settings;
	i32              mask = 0;
	u32              scene[2];
	bool             hashed[2] = {false, false}; // The scene without and with vertex colors, hashed once
	for (i32 slot = 0; slot < MESH_MAP_COUNT; ++slot) {
		if (util_bake_maps[slot] == NULL || util_bake_sigs[slot] == 0) {
			continue;
		}
		i32 colors = util_bake_uses_colors(slot, s) ? 1 : 0;
		if (!hashed[colors]) {
			scene[colors]  = util_bake_scene_hash(s, colors);
			hashed[colors] = true;
		}
		if (util_bake_combine(util_bake_settings_hash(slot, s), scene[colors]) != util_bake_sigs[slot]) {
			mask |= 1 << slot;
		}
	}
	return mask;
}

bool util_bake_outdated(i32 slot) {
	return slot >= 0 && slot < MESH_MAP_COUNT && ((util_bake_outdated_mask() >> slot) & 1);
}

static void util_bake_set_map(i32 slot, gpu_texture_t *tex, bool history) {
	if (history) {
		history_step_t *step = history_push(HISTORY_ACTION_BAKE_MESH_MAP);
		step->mesh_map_slot  = slot;
		step->mesh_map_tex   = util_bake_maps[slot]; // Kept for undo
		step->mesh_map_sig   = util_bake_sigs[slot];
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
	u32            sig   = util_bake_sigs[slot];
	util_bake_maps[slot] = step->mesh_map_tex;
	util_bake_sigs[slot] = step->mesh_map_sig;
	step->mesh_map_tex   = tex;
	step->mesh_map_sig   = sig;
	util_bake_maps_changed();
}

void util_bake_clear(i32 slot) {
	if (slot < 0 || slot >= MESH_MAP_COUNT || util_bake_maps[slot] == NULL) {
		return;
	}
	util_bake_set_map(slot, NULL, true);
	util_bake_sigs[slot] = 0;
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
		job->sig = util_bake_combine(job->settings_hash, util_bake_scene_hash(&job->settings, util_bake_uses_colors(job->slot, &job->settings)));
		if (job->quick) { // A fast preview, the full bake follows
			bake_settings_t *s   = &job->settings;
			s->res               = s->res / 4 > 128 ? s->res / 4 : 128;
			s->dilation          = (s->dilation + 3) / 4;
			s->aa                = 1;
			s->occlusion_samples = s->occlusion_samples / 4 > 1 ? s->occlusion_samples / 4 : 1;
			s->thickness_samples = s->thickness_samples / 4 > 1 ? s->thickness_samples / 4 : 1;
		}
		i32  res            = job->settings.res > 0 ? job->settings.res : 1;
		bool colors         = job->slot == MESH_MAP_ID && job->settings.id_source == MESH_MAP_ID_VERTEX_COLOR;
		bool vertex_curv    = job->slot == MESH_MAP_CURVATURE && job->settings.curvature_mode == MESH_MAP_CURVATURE_MESH;
		job->raster         = util_bake_raster_create(&job->settings, res, colors, util_bake_slot_casts_rays(job->slot), vertex_curv);
		job->out            = calloc((size_t)res * res * 4, sizeof(f32));
		job->rows_per_frame = util_bake_slot_casts_rays(job->slot) || job->raster->high.count > 0 ? 1 : res;
	}
	else if (job->rows_done < job->raster->res) {
		util_bake_step_rows(job);
	}
	else {
		if (job->automatic && util_bake_maps[job->slot] == NULL) { // Cleared or undone to empty while it baked
			util_bake_finish(job);
			if (in_use) {
				draw_begin(current, false, 0);
			}
			return;
		}
		util_bake_dilate(job->out, job->raster->tri, job->raster->res, job->settings.dilation, job->slot);
		gpu_texture_t *tex = util_bake_upload(job->slot, job->out, job->raster->res);
		util_bake_set_map(job->slot, tex, !job->automatic); // Auto rebake would fill the history while a setting is dragged
		util_bake_sigs[job->slot] = job->quick ? job->sig | 1 : job->sig;
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
	bake_job_t *job    = calloc(1, sizeof(bake_job_t));
	job->slot          = slot;
	job->settings      = util_bake_settings;
	job->settings_hash = util_bake_settings_hash(slot, &util_bake_settings);
	job->done          = done;
	job->done_data     = done_data;
	util_bake_push(job);
	return true;
}

// Queues the slots whose map is outdated, returns how many
i32 util_bake_run_outdated() {
	i32 mask  = util_bake_outdated_mask();
	i32 count = 0;
	for (i32 slot = 0; slot < MESH_MAP_COUNT; ++slot) {
		if ((mask >> slot) & 1) {
			util_bake_run(slot, NULL, NULL);
			count++;
		}
	}
	return count;
}

static void util_bake_run_auto(i32 slot, u32 settings_hash, u32 sig) {
	bool quick = util_bake_settings.quick_rebake && util_bake_settings.res >= UTIL_BAKE_QUICK_MIN;
	for (i32 pass = quick ? 0 : 1; pass < 2; ++pass) {
		bake_job_t *job    = calloc(1, sizeof(bake_job_t));
		job->slot          = slot;
		job->settings      = util_bake_settings;
		job->settings_hash = settings_hash;
		job->queued_sig    = sig;
		job->automatic     = true;
		job->quick         = pass == 0;
		util_bake_push(job);
	}
}

// Auto rebake jobs of a slot made for older settings or another scene start over
static void util_bake_drop_stale(i32 slot, u32 sig) {
	i32 kept = 0;
	for (i32 i = 0; i < util_bake_jobs_count; ++i) {
		bake_job_t *job = util_bake_jobs[i];
		if (job->slot == slot && job->automatic && job->queued_sig != sig) {
			util_bake_job_free(job);
		}
		else {
			util_bake_jobs[kept++] = job;
		}
	}
	util_bake_jobs_count = kept;
	bake_job_t *job      = util_bake_job;
	if (job != NULL && job->slot == slot && job->automatic && (job->raster != NULL ? job->sig != sig : job->queued_sig != sig)) {
		util_bake_job_free(job);
		util_bake_job = NULL; // util_bake_update moves on to the next job
	}
}

// Called every frame: bakes the outdated maps again once their signature stops changing
void util_bake_auto_update() {
	util_bake_roles_update();
	bake_settings_t *s   = &util_bake_settings;
	f64              now = iron_time();
	if (!s->auto_rebake || now - util_bake_auto_last < 0.1) {
		return;
	}
	util_bake_auto_last = now;
	u32  scene[2];
	bool hashed[2] = {false, false};
	for (i32 slot = 0; slot < MESH_MAP_COUNT; ++slot) {
		if (util_bake_maps[slot] == NULL || util_bake_sigs[slot] == 0) {
			continue;
		}
		i32 colors = util_bake_uses_colors(slot, s) ? 1 : 0;
		if (!hashed[colors]) {
			scene[colors]  = util_bake_scene_hash(s, colors);
			hashed[colors] = true;
		}
		u32 settings_hash = util_bake_settings_hash(slot, s);
		u32 sig           = util_bake_combine(settings_hash, scene[colors]);
		util_bake_drop_stale(slot, sig);
		if (util_bake_state(slot, NULL) != BAKE_STATE_IDLE || sig == util_bake_sigs[slot] || sig == util_bake_skip_sigs[slot]) {
			continue;
		}
		if (sig != util_bake_seen_sigs[slot]) {
			util_bake_seen_sigs[slot] = sig;
			util_bake_seen_time[slot] = now;
			continue;
		}
		if (now - util_bake_seen_time[slot] >= UTIL_BAKE_AUTO_DELAY) {
			util_bake_run_auto(slot, settings_hash, sig);
		}
	}
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
// Auto rebake leaves a cancelled slot alone until its settings or the scene change again
static void util_bake_skip(i32 slot) {
	if (slot >= 0) {
		util_bake_skip_sigs[slot] = util_bake_signature(slot);
	}
}

void util_bake_cancel_job(i32 slot, void *owner) {
	util_bake_skip(slot);
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
		util_bake_skip(util_bake_jobs[i]->slot);
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
		util_bake_skip(util_bake_job->slot);
		util_bake_job_free(util_bake_job);
		util_bake_job = NULL;
	}
	sys_remove_update(util_bake_update);
}

// New or opened project
void util_bake_reset() {
	util_bake_cancel();
	util_bake_settings = (bake_settings_t)UTIL_BAKE_DEFAULT_SETTINGS;
	if (util_bake_uv_islands != NULL) {
		gpu_delete_texture(util_bake_uv_islands);
		util_bake_uv_islands = NULL;
	}
	for (i32 i = 0; i < MESH_MAP_COUNT; ++i) {
		if (util_bake_maps[i] != NULL) {
			gpu_delete_texture(util_bake_maps[i]);
			util_bake_maps[i] = NULL;
		}
		util_bake_sigs[i]      = 0;
		util_bake_skip_sigs[i] = 0;
		util_bake_seen_sigs[i] = 0;
	}
	util_bake_roles_dirty = true;
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

// A json string literal of s, free() it
static char *util_bake_json_string(char *s) {
	s         = s != NULL ? s : "";
	char *out = malloc(string_length(s) * 2 + 3);
	i32   n   = 0;
	out[n++]  = '"';
	for (char *c = s; *c != '\0'; ++c) {
		if (*c == '"' || *c == '\\' || *c == '\n') {
			out[n++] = '\\';
			out[n++] = *c == '\n' ? 'n' : *c;
		}
		else {
			out[n++] = *c;
		}
	}
	out[n++] = '"';
	out[n]   = '\0';
	return out;
}

char *util_bake_settings_encode() {
	bake_settings_t *s          = &util_bake_settings;
	char            *high_polys = util_bake_json_string(s->high_polys);
	char            *json       = string("{\"res\":%d,\"dilation\":%d,\"occlusion_samples\":%d,\"occlusion_distance\":%.9g,\"thickness_samples\":%d,"
	                                                      "\"thickness_distance\":%.9g,\"curvature_strength\":%.9g,\"id_source\":%d,\"maps\":%d,\"aa\":%d,",
	                                     s->res, s->dilation, s->occlusion_samples, s->occlusion_distance, s->thickness_samples, s->thickness_distance, s->curvature_strength,
	                                     s->id_source, s->maps, s->aa);
	json = string("%s\"front_distance\":%.9g,\"rear_distance\":%.9g,\"average_normals\":%d,\"ignore_backface\":%d,\"match_by_name\":%d,\"high_polys\":%s,",
	              json, s->front_distance, s->rear_distance, s->average_normals, s->ignore_backface, s->match_by_name, high_polys);
	json = string("%s\"curvature_mode\":%d,\"auto_rebake\":%d,\"quick_rebake\":%d,\"cage\":%d,", json, s->curvature_mode, s->auto_rebake, s->quick_rebake,
	              s->cage);
	json = string("%s\"skew_base\":%d,\"edge_protection\":%d,\"paint_value\":%.9g,\"cage_range\":%.9g,\"baked\":\"", json, s->skew_base, s->edge_protection,
	              s->paint_value, s->cage_range);
	free(high_polys);
	// What each slot was baked from, so a reopened project still knows which maps are outdated
	for (i32 slot = 0; slot < MESH_MAP_COUNT; ++slot) {
		json = string("%s%s%x", json, slot > 0 ? " " : "", util_bake_sigs[slot]);
	}
	return string("%s\"}", json);
}

static void util_bake_settings_read_i32(any_map_t *m, char *key, i32 *v) {
	char *s = any_map_get(m, key);
	if (s != NULL) {
		*v = parse_int(s);
	}
}

static void util_bake_settings_read_bool(any_map_t *m, char *key, bool *v) {
	char *s = any_map_get(m, key);
	if (s != NULL) {
		*v = parse_int(s) != 0;
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
	util_bake_settings_read_i32(m, "aa", &s->aa);
	util_bake_settings_read_f32(m, "front_distance", &s->front_distance);
	util_bake_settings_read_f32(m, "rear_distance", &s->rear_distance);
	util_bake_settings_read_bool(m, "average_normals", &s->average_normals);
	util_bake_settings_read_bool(m, "ignore_backface", &s->ignore_backface);
	util_bake_settings_read_bool(m, "match_by_name", &s->match_by_name);
	util_bake_settings_read_i32(m, "curvature_mode", &s->curvature_mode);
	util_bake_settings_read_bool(m, "auto_rebake", &s->auto_rebake);
	util_bake_settings_read_bool(m, "quick_rebake", &s->quick_rebake);
	util_bake_settings_read_i32(m, "cage", &s->cage);
	util_bake_settings_read_i32(m, "skew_base", &s->skew_base);
	util_bake_settings_read_bool(m, "edge_protection", &s->edge_protection);
	util_bake_settings_read_f32(m, "paint_value", &s->paint_value);
	util_bake_settings_read_f32(m, "cage_range", &s->cage_range);
	char *baked = any_map_get(m, "baked");
	for (i32 slot = 0; baked != NULL && slot < MESH_MAP_COUNT && *baked != '\0'; ++slot) {
		util_bake_sigs[slot] = (u32)strtoul(baked, &baked, 16);
	}
	char *high_polys = any_map_get(m, "high_polys");
	if (high_polys != NULL) {
		s->high_polys = high_polys;
	}
	s->res            = s->res < 1 ? 1 : s->res > 16384 ? 16384 : s->res;
	s->dilation       = s->dilation < 0 ? 0 : s->dilation;
	s->id_source      = s->id_source == MESH_MAP_ID_VERTEX_COLOR || s->id_source == MESH_MAP_ID_UV_ISLAND ? s->id_source : MESH_MAP_ID_OBJECT;
	s->curvature_mode = s->curvature_mode == MESH_MAP_CURVATURE_MESH ? MESH_MAP_CURVATURE_MESH : MESH_MAP_CURVATURE_NORMALS;
	s->cage           = s->cage == BAKE_CAGE_AUTO ? BAKE_CAGE_AUTO : BAKE_CAGE_DISTANCE;
	s->skew_base      = s->skew_base == BAKE_SKEW_BASE_TRIANGLE ? BAKE_SKEW_BASE_TRIANGLE : BAKE_SKEW_BASE_MESH;
	s->paint_value    = s->paint_value < 0.0f ? 0.0f : s->paint_value > 1.0f ? 1.0f : s->paint_value;
	s->cage_range     = s->cage_range < 0.0f ? 0.0f : s->cage_range;
	s->maps &= (1 << MESH_MAP_COUNT) - 1;
	s->aa = s->aa >= 8 ? 8 : s->aa >= 4 ? 4 : s->aa >= 2 ? 2 : 1;
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
				u8 b                  = pixels->buffer[i];
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
