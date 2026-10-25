#include "../global.h"

// Faces of the geometry mask the select tool edits (LAY-11), a translucent overlay of the selected triangles in
// the 3D view. Depth tested against the scene, drawn after the grid.

#define RENDER_GEOMETRY_SELECT_PIPES 8

static gpu_pipeline_t *render_geometry_select_pipes[RENDER_GEOMETRY_SELECT_PIPES];
static i32             render_geometry_select_pipes_count = 0;
static i32             render_geometry_select_wvp_loc;
static i32             render_geometry_select_color_loc;

// Index buffer of the selected triangles of an object with some of its faces selected
typedef struct render_geometry_select_cache {
	mesh_object_t *object;
	mesh_data_t   *data;
	i32            triangles;
	u32            hash;
	gpu_buffer_t  *indices;
} render_geometry_select_cache_t;

static any_array_t *render_geometry_select_caches = NULL;

static gpu_pipeline_t *render_geometry_select_pipe(mesh_data_t *data) {
	// Vertex layout differs for meshes with a second uv set or vertex colors
	gpu_vertex_structure_t *structure = &data->_->structure;
	for (i32 i = 0; i < render_geometry_select_pipes_count; ++i) {
		gpu_vertex_structure_t *vs    = render_geometry_select_pipes[i]->input_layout;
		bool                    match = vs->size == structure->size;
		for (i32 j = 0; match && j < vs->size; ++j) {
			match = vs->elements[j].data == structure->elements[j].data;
		}
		if (match) {
			return render_geometry_select_pipes[i];
		}
	}
	if (render_geometry_select_pipes_count == RENDER_GEOMETRY_SELECT_PIPES) {
		return NULL;
	}
	gpu_pipeline_t *pipe          = gpu_create_pipeline();
	pipe->vertex_shader           = sys_get_shader("geometry_select.vert");
	pipe->fragment_shader         = sys_get_shader("geometry_select.frag");
	gpu_vertex_structure_t *vs    = ALLOC_INIT(gpu_vertex_structure_t, {0});
	*vs                           = *structure;
	pipe->input_layout            = vs;
	pipe->depth_write             = false;
	pipe->depth_mode              = GPU_COMPARE_MODE_LESS; // The vertex shader pulls the faces in front of their own depth
	pipe->cull_mode               = GPU_CULL_MODE_NONE;
	pipe->blend_source            = GPU_BLEND_SOURCE_ALPHA;
	pipe->blend_destination       = GPU_BLEND_INV_SOURCE_ALPHA;
	pipe->alpha_blend_source      = GPU_BLEND_ZERO;
	pipe->alpha_blend_destination = GPU_BLEND_ONE;
	pipe->color_attachment_count  = 1;
	pipe->color_attachment[0]     = GPU_TEXTURE_FORMAT_RGBA64;
	pipe->depth_attachment_bits   = 32;
	gpu_pipeline_compile(pipe);
	if (render_geometry_select_pipes_count == 0) {
		pipes_offset                     = 0;
		render_geometry_select_wvp_loc   = pipes_get_constant_location("float4x4");
		render_geometry_select_color_loc = pipes_get_constant_location("float4");
	}
	render_geometry_select_pipes[render_geometry_select_pipes_count++] = pipe;
	return pipe;
}

static void render_geometry_select_cache_free(render_geometry_select_cache_t *c) {
	if (c->indices != NULL) {
		gpu_delete_buffer(c->indices);
	}
	free(c);
}

// Entries of objects that are gone
static void render_geometry_select_prune() {
	for (i32 i = 0; render_geometry_select_caches != NULL && i < render_geometry_select_caches->length; ++i) {
		render_geometry_select_cache_t *c = render_geometry_select_caches->buffer[i];
		if (array_index_of(g_project->_->paint_objects, c->object) < 0) {
			array_splice(render_geometry_select_caches, i, 1);
			render_geometry_select_cache_free(c);
			--i;
		}
	}
}

// Index buffer of the selected triangles, rebuilt when the selection or the mesh changed
static gpu_buffer_t *render_geometry_select_indices(mesh_object_t *o, geometry_mask_faces_t *f) {
	u32 hash = 2166136261u;
	for (i32 i = 0; i < f->bits->length; ++i) {
		hash ^= f->bits->buffer[i];
		hash *= 16777619u;
	}
	if (render_geometry_select_caches == NULL) {
		render_geometry_select_caches = any_array_create(0);
	}
	render_geometry_select_cache_t *c = NULL;
	for (i32 i = 0; i < render_geometry_select_caches->length; ++i) {
		render_geometry_select_cache_t *e = render_geometry_select_caches->buffer[i];
		if (e->object == o) {
			c = e;
			break;
		}
	}
	if (c != NULL && c->data == o->data && c->triangles == f->triangles && c->hash == hash) {
		return c->indices;
	}
	if (c == NULL) {
		c = ALLOC_INIT(render_geometry_select_cache_t, {.object = o, .indices = NULL});
		any_array_push(render_geometry_select_caches, c);
	}
	if (c->indices != NULL) {
		gpu_delete_buffer(c->indices);
		c->indices = NULL;
	}
	c->data      = o->data;
	c->triangles = f->triangles;
	c->hash      = hash;
	if (f->count > 0) {
		u32          *ind = o->data->index_array->buffer;
		gpu_buffer_t *ib  = gpu_create_index_buffer(f->count * 3);
		u32          *out = gpu_index_buffer_lock(ib);
		i32           n   = 0;
		for (i32 t = 0; t < f->triangles && n < f->count * 3; ++t) {
			if (f->bits->buffer[t >> 3] & (1 << (t & 7))) {
				out[n++] = ind[t * 3];
				out[n++] = ind[t * 3 + 1];
				out[n++] = ind[t * 3 + 2];
			}
		}
		gpu_index_buffer_unlock(ib);
		c->indices = ib;
	}
	return c->indices;
}

void render_geometry_select() {
	render_geometry_select_prune();
	geometry_mask_t *g = util_geometry_mask_shown();
	if (g == NULL || base_player || g_context->capturing_screenshot || g_context->paint2d) {
		return;
	}
	u32    col = g_theme->HIGHLIGHT_COL;
	mat4_t vp  = scene_camera->vp; // Jittered like the depth of the scene
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *o = g_project->_->paint_objects->buffer[i];
		if (!o->base->visible || o->data == NULL || o->data->index_array->length < 3 || !make_material_is_default(o->material)) {
			continue;
		}
		geometry_mask_cover_t cover = slot_layer_geometry_mask_selection(g, o);
		if (cover == GEOMETRY_MASK_COVER_NONE) {
			continue;
		}
		gpu_buffer_t *indices = o->data->_->index_buffer;
		if (cover == GEOMETRY_MASK_COVER_SOME) {
			indices = render_geometry_select_indices(o, slot_layer_geometry_mask_faces(g, o, true));
		}
		gpu_pipeline_t *pipe = render_geometry_select_pipe(o->data);
		if (indices == NULL || pipe == NULL) {
			continue;
		}
		gpu_set_pipeline(pipe);
		gpu_set_mat4(render_geometry_select_wvp_loc, mat4_mult_mat(o->base->transform->world_unpack, vp));
		gpu_set_float4(render_geometry_select_color_loc, color_get_rb(col) / 255.0, color_get_gb(col) / 255.0, color_get_bb(col) / 255.0, 0.35);
		gpu_set_vertex_buffer(o->data->_->vertex_buffer);
		gpu_set_index_buffer(indices);
		gpu_draw();
	}
}
