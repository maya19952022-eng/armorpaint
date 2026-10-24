
#include "../global.h"

// Geometry mask of layers (LAY-11). The tiles and objects of the mask are drawn into a texture in
// the uv space of the layer, white where the layer applies. Merge, flatten and export multiply it
// with the layer masks and the paint shader multiplies the stroke with it. The viewport tests the
// object of each fragment instead, see make_mesh.c.

static gpu_texture_t *util_geometry_mask_temp = NULL; // Layer masks times the geometry mask

// Objects drawn into the uv space of the layer: the object of an object mask, the objects of an atlas or all of them
static bool *util_geometry_mask_space(slot_layer_t *owner) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	bool                  *in      = calloc(objects->length > 0 ? objects->length : 1, sizeof(bool));
	i32                    mask    = slot_layer_get_object_mask(owner);
	if (mask <= 0) {
		for (i32 i = 0; i < objects->length; ++i) {
			in[i] = true;
		}
	}
	else if (mask <= objects->length) {
		in[mask - 1] = true;
	}
	else {
		mesh_object_t_array_t *atlas = project_get_atlas_objects(mask);
		for (i32 i = 0; i < objects->length; ++i) {
			in[i] = array_index_of(atlas, objects->buffer[i]) >= 0;
		}
		if (atlas != objects) {
			array_delete(atlas);
		}
	}
	return in;
}

static vertex_array_t *util_geometry_mask_uvs(slot_layer_t *owner, mesh_object_t *o) {
	vertex_array_t *va = owner->uv_map == 1 ? mesh_data_get_vertex_array(o->data, "tex1") : NULL;
	return va != NULL ? va : mesh_data_get_vertex_array(o->data, "tex");
}

static u32 util_geometry_mask_hash_add(u32 h, u32 v) {
	for (i32 i = 0; i < 4; ++i) {
		h ^= (v >> (i * 8)) & 0xff;
		h *= 16777619u;
	}
	return h;
}

// Everything the texture is drawn from, a different hash rebuilds it
static u32 util_geometry_mask_hash(slot_layer_t *owner, i32 width, i32 height, bool *in) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	bool                   udim    = util_mesh_udim_layer(owner);
	u32                    h       = 2166136261u;
	h                              = util_geometry_mask_hash_add(h, width);
	h                              = util_geometry_mask_hash_add(h, height);
	h                              = util_geometry_mask_hash_add(h, slot_layer_geometry_mask_keeps_empty(owner));
	h                              = util_geometry_mask_hash_add(h, udim ? util_mesh_atlas_stride() : 0);
	h                              = util_geometry_mask_hash_add(h, g_config->dilate_radius);
	for (i32 i = 0; i < objects->length; ++i) {
		if (!in[i]) {
			continue;
		}
		mesh_object_t  *o  = objects->buffer[i];
		vertex_array_t *va = util_geometry_mask_uvs(owner, o);
		h                  = util_geometry_mask_hash_add(h, i);
		h                  = util_geometry_mask_hash_add(h, slot_layer_geometry_mask_object(owner, o));
		h                  = util_geometry_mask_hash_add(h, udim ? util_mesh_atlas_slot(o->base) : 0);
		h                  = util_geometry_mask_hash_add(h, (u32)(uintptr_t)o->data);
		h                  = util_geometry_mask_hash_add(h, o->data->index_array->length);
		if (va != NULL) {
			// The uvs can be edited in place
			u32 sum = va->values->length;
			for (i32 j = 0; j < va->values->length; ++j) {
				sum = sum * 31 + (u16)va->values->buffer[j];
			}
			h = util_geometry_mask_hash_add(h, sum);
		}
	}
	return h;
}

// Triangles of the objects whose contribution matches on, as fills or as outlines that cover the padding of the islands
static void util_geometry_mask_draw_objects(slot_layer_t *owner, gpu_texture_t *target, bool *in, bool on, bool outline) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	bool                   udim    = util_mesh_udim_layer(owner);
	i32                    stride  = util_mesh_atlas_stride();
	f32                    width   = g_config->dilate_radius > 1 ? g_config->dilate_radius * 2.0 : 2.0;
	draw_set_color(on ? 0xffffffff : 0xff000000);
	for (i32 i = 0; i < objects->length; ++i) {
		mesh_object_t *o = objects->buffer[i];
		if (!in[i] || slot_layer_geometry_mask_object(owner, o) != on) {
			continue;
		}
		vertex_array_t *va = util_geometry_mask_uvs(owner, o);
		if (va == NULL) {
			continue;
		}
		// Shared layers of udim tiles keep every tile in its own atlas slot, like the merged mesh
		f32 scale    = 1.0;
		f32 offset_x = 0.0;
		f32 offset_y = 0.0;
		if (udim) {
			i32 slot = util_mesh_atlas_slot(o->base);
			if (slot >= 0) {
				scale    = 1.0 / stride;
				offset_x = (slot % stride) / (f32)stride;
				offset_y = (slot / stride) / (f32)stride;
			}
		}
		f32          fx  = scale / 32767.0 * target->width;
		f32          fy  = scale / 32767.0 * target->height;
		f32          ox  = offset_x * target->width;
		f32          oy  = offset_y * target->height;
		i16_array_t *tex = va->values;
		u32_array_t *ind = o->data->index_array;
		for (i32 t = 0; t < ind->length / 3; ++t) {
			f32 x[3];
			f32 y[3];
			for (i32 k = 0; k < 3; ++k) {
				u32 v = ind->buffer[t * 3 + k];
				x[k]  = tex->buffer[v * 2] * fx + ox;
				y[k]  = tex->buffer[v * 2 + 1] * fy + oy;
			}
			if (outline) {
				draw_line(x[0], y[0], x[1], y[1], width);
				draw_line(x[1], y[1], x[2], y[2], width);
				draw_line(x[2], y[2], x[0], y[0], width);
			}
			else {
				draw_filled_triangle(x[0], y[0], x[1], y[1], x[2], y[2]);
			}
		}
	}
}

static void util_geometry_mask_draw(slot_layer_t *owner, gpu_texture_t *target, bool *in) {
	bool keeps_empty = slot_layer_geometry_mask_keeps_empty(owner);

	// The pass that is open is ended, a render path pass begins again with its next target
	gpu_texture_t *current = _draw_current;
	u32            color   = draw_get_color();
	bool           resume  = false;
	if (gpu_in_use) {
		if (_render_path_current_image != NULL) {
			render_path_end();
		}
		else {
			draw_end();
			resume = true;
		}
	}

	// Islands where the layer applies win over the others where they share texels
	draw_begin(target, true, keeps_empty ? 0xffffffff : 0xff000000);
	draw_set_pipeline(NULL);
	if (keeps_empty) {
		util_geometry_mask_draw_objects(owner, target, in, false, true);
	}
	util_geometry_mask_draw_objects(owner, target, in, true, true);
	util_geometry_mask_draw_objects(owner, target, in, false, false);
	util_geometry_mask_draw_objects(owner, target, in, true, false);
	draw_end();

	if (resume) {
		draw_begin(current, false, 0);
	}
	draw_set_color(color);
}

// Geometry mask of the layer in its uv space, white where it applies, NULL without a geometry mask. Built when
// the mask, the objects or their uvs changed.
gpu_texture_t *util_geometry_mask_texture(slot_layer_t *l) {
	slot_layer_t *owner = slot_layer_geometry_owner(l);
	if (!slot_layer_has_geometry_mask(owner)) {
		if (owner->texpaint_geometry != NULL) {
			gpu_delete_texture(owner->texpaint_geometry);
			owner->texpaint_geometry = NULL;
		}
		return NULL;
	}

	i32 width  = owner->texpaint != NULL ? owner->texpaint->width : config_get_texture_res_x();
	i32 height = owner->texpaint != NULL ? owner->texpaint->height : config_get_texture_res_y();
	if (owner->texpaint_geometry != NULL && (owner->texpaint_geometry->width != width || owner->texpaint_geometry->height != height)) {
		gpu_delete_texture(owner->texpaint_geometry);
		owner->texpaint_geometry = NULL;
	}

	bool *in   = util_geometry_mask_space(owner);
	u32   hash = util_geometry_mask_hash(owner, width, height, in);
	if (owner->texpaint_geometry == NULL || owner->texpaint_geometry_hash != hash) {
		if (owner->texpaint_geometry == NULL) {
			owner->texpaint_geometry = gpu_create_render_target(width, height, GPU_TEXTURE_FORMAT_RGBA32);
		}
		util_geometry_mask_draw(owner, owner->texpaint_geometry, in);
		owner->texpaint_geometry_hash = hash;
	}
	free(in);
	return owner->texpaint_geometry;
}

// Texture built by the last util_geometry_mask_texture call, for shaders bound in the middle of a pass
gpu_texture_t *util_geometry_mask_cached(slot_layer_t *l) {
	slot_layer_t *owner = slot_layer_geometry_owner(l);
	return slot_layer_has_geometry_mask(owner) ? owner->texpaint_geometry : NULL;
}

// Painting is limited to the geometry mask. Fill layers and fill masks are filled whole when refilled and
// masked when composited, so changing the mask does not need a refill.
bool util_geometry_mask_paints(slot_layer_t *l) {
	return l->fill_material == NULL && l->texpaint_sculpt == NULL && !slot_layer_is_group(l) && slot_layer_has_geometry_mask(l);
}

// The layer applies on at least one paint object
bool util_geometry_mask_any_object(slot_layer_t *l) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	for (i32 i = 0; i < objects->length; ++i) {
		if (slot_layer_geometry_mask_object(l, objects->buffer[i])) {
			return true;
		}
	}
	return false;
}

// Mask of a layer when merging, flattening or exporting it: the layer masks in mask (empty_white when it has none)
// times its geometry mask
gpu_texture_t *util_geometry_mask_apply(slot_layer_t *l, gpu_texture_t *mask) {
	gpu_texture_t *g = util_geometry_mask_texture(l);
	if (g == NULL) {
		return mask;
	}
	render_target_t *empty = any_map_get(render_path_render_targets, "empty_white");
	if (mask == NULL || mask == empty->_image) {
		return g;
	}
	if (util_geometry_mask_temp != NULL && (util_geometry_mask_temp->width != mask->width || util_geometry_mask_temp->height != mask->height)) {
		gpu_delete_texture(util_geometry_mask_temp);
		util_geometry_mask_temp = NULL;
	}
	if (util_geometry_mask_temp == NULL) {
		util_geometry_mask_temp = gpu_create_render_target(mask->width, mask->height, GPU_TEXTURE_FORMAT_RGBA32);
	}
	_gpu_begin(util_geometry_mask_temp, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_merge_mask);
	gpu_set_texture(pipes_tex0_merge_mask, g);
	gpu_set_texture(pipes_texa_merge_mask, mask);
	gpu_set_float(pipes_opac_merge_mask, 1.0);
	gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MULTIPLY);
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
	return util_geometry_mask_temp;
}

// Before a merge: the geometry masks of the layer and of its group go into its opacity and the mask of the layer
// is cleared, the result looks the same
void util_geometry_mask_bake(slot_layer_t *l) {
	if (!slot_layer_is_layer(l) || slot_layer_is_filter(l)) {
		return;
	}
	gpu_texture_t *g = util_geometry_mask_texture(l);
	if (g != NULL) {
		layers_make_temp_img();
		draw_begin(layers_temp_image, false, 0);
		draw_set_pipeline(pipes_copy);
		draw_image(l->texpaint, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();

		_gpu_begin(l->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_apply_mask);
		gpu_set_texture(pipes_tex0_mask, layers_temp_image);
		gpu_set_texture(pipes_texa_mask, g);
		gpu_set_float(pipes_opac_apply_mask, 1.0);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}
	slot_layer_geometry_mask_free(&l->geometry_mask);
}

// A paint object was renamed, masks select objects by name
void util_geometry_mask_rename_object(char *old_name, char *new_name) {
	if (g_project == NULL || g_project->_ == NULL || g_project->_->layers == NULL || old_name == NULL || new_name == NULL) {
		return;
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		string_array_t *objects = g_project->_->layers->buffer[i]->geometry_mask.objects;
		i32             j       = objects != NULL ? string_array_index_of(objects, old_name) : -1;
		if (j >= 0) {
			free(objects->buffer[j]);
			objects->buffer[j] = string_copy(new_name);
		}
	}
}

// Edits from the layers tab and scripts, each one is an undo step on the layer or group the mask belongs to

static void util_geometry_mask_edited(slot_layer_t *owner, geometry_mask_t g) {
	history_layer_geometry_mask(owner);
	slot_layer_geometry_mask_free(&owner->geometry_mask);
	owner->geometry_mask = g;
	make_material_parse_mesh_material();
	make_material_parse_paint_material(false);
	g_context->layers_preview_dirty = true;
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
}

void util_geometry_mask_set_mode(slot_layer_t *l, geometry_mask_mode_t mode) {
	slot_layer_t *owner = slot_layer_geometry_owner(l);
	mode                = mode == GEOMETRY_MASK_EXCLUDE ? GEOMETRY_MASK_EXCLUDE : GEOMETRY_MASK_INCLUDE;
	if (owner->geometry_mask.mode == mode) {
		return;
	}
	geometry_mask_t g = slot_layer_geometry_mask_copy(&owner->geometry_mask);
	g.mode            = mode;
	util_geometry_mask_edited(owner, g);
}

void util_geometry_mask_set_object(slot_layer_t *l, char *name, bool selected) {
	slot_layer_t *owner = slot_layer_geometry_owner(l);
	bool          has   = name != NULL && owner->geometry_mask.objects != NULL && string_array_index_of(owner->geometry_mask.objects, name) >= 0;
	if (name == NULL || has == selected) {
		return;
	}
	geometry_mask_t g = slot_layer_geometry_mask_copy(&owner->geometry_mask);
	if (selected) {
		if (g.objects == NULL) {
			g.objects = string_array_create(0);
		}
		string_array_push(g.objects, string_copy(name));
	}
	else {
		i32 i = string_array_index_of(g.objects, name);
		free(g.objects->buffer[i]);
		array_splice((any_array_t *)g.objects, i, 1);
	}
	util_geometry_mask_edited(owner, g);
}

void util_geometry_mask_set_tile(slot_layer_t *l, i32 tile, bool selected) {
	slot_layer_t *owner = slot_layer_geometry_owner(l);
	bool          has   = owner->geometry_mask.tiles != NULL && i32_array_index_of(owner->geometry_mask.tiles, tile) >= 0;
	if (has == selected) {
		return;
	}
	geometry_mask_t g = slot_layer_geometry_mask_copy(&owner->geometry_mask);
	if (selected) {
		if (g.tiles == NULL) {
			g.tiles = i32_array_create(0);
		}
		i32_array_push(g.tiles, tile);
	}
	else {
		i32_array_splice(g.tiles, i32_array_index_of(g.tiles, tile), 1);
	}
	util_geometry_mask_edited(owner, g);
}

// Back to no geometry mask, include mode with nothing selected
void util_geometry_mask_clear(slot_layer_t *l) {
	slot_layer_t *owner = slot_layer_geometry_owner(l);
	if (slot_layer_geometry_mask_is_empty(&owner->geometry_mask) && owner->geometry_mask.mode == GEOMETRY_MASK_INCLUDE) {
		return;
	}
	geometry_mask_t g = {.mode = GEOMETRY_MASK_INCLUDE, .tiles = NULL, .objects = NULL};
	util_geometry_mask_edited(owner, g);
}

// UDIM tiles of the paint objects in ascending order, empty when no object has a tile suffix
i32_array_t *util_geometry_mask_project_tiles() {
	i32_array_t *tiles = i32_array_create(0);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		i32 tile = util_mesh_udim_tile(g_project->_->paint_objects->buffer[i]->base->name);
		if (tile < 0 || i32_array_index_of(tiles, tile) >= 0) {
			continue;
		}
		i32_array_push(tiles, tile);
		for (i32 j = tiles->length - 1; j > 0 && tiles->buffer[j - 1] > tile; --j) {
			tiles->buffer[j]     = tiles->buffer[j - 1];
			tiles->buffer[j - 1] = tile;
		}
	}
	return tiles;
}
