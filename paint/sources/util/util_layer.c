
#include "../global.h"

#if defined(IRON_ANDROID) || defined(IRON_IOS)
i32 layers_max_layers = 18;
#else
i32 layers_max_layers = 255;
#endif

uv_type_t _layers_uv_type;
mat4_t    _layers_decal_mat;
i32       _layers_position;
i32       _layers_base_color;
f32       _layers_occlusion;
f32       _layers_roughness;
f32       _layers_metallic;

static void util_layer_draw_preview(slot_layer_t *l, gpu_texture_t *target) {
	paint_tex_t type = tab_layers_preview_tex;
	if (slot_layer_is_mask(l) || (type == PAINT_TEX_NORMAL && l->texpaint_nor == NULL) ||
	    (type >= PAINT_TEX_OCCLUSION && l->texpaint_pack == NULL)) {
		type = PAINT_TEX_BASE;
	}

	if (type == PAINT_TEX_BASE) {
		draw_begin(target, true, 0x00000000);
		// draw_set_pipeline(l.is_mask() ? pipes_copy8 : pipes_copy);
		draw_set_pipeline(pipes_copy); // texpaint_preview is always RGBA32 for now
		draw_scaled_image(l->texpaint, 0, 0, target->width, target->height);
		draw_set_pipeline(NULL);
		draw_end();
		return;
	}

	gpu_texture_t *source = type == PAINT_TEX_OPACITY ? l->texpaint : type == PAINT_TEX_NORMAL ? l->texpaint_nor : l->texpaint_pack;
	i32            channel = type == PAINT_TEX_OCCLUSION   ? 1
	                         : type == PAINT_TEX_ROUGHNESS ? 2
	                         : type == PAINT_TEX_METALLIC  ? 3
	                         : type == PAINT_TEX_NORMAL    ? 5
	                                                       : 4; // Opacity, height
	draw_begin(target, true, 0xff000000); // view2d pipe keeps dst alpha
	draw_set_pipeline(ui_view2d_pipe);
	gpu_set_int(ui_view2d_channel_loc, channel);
	draw_scaled_image(source, 0, 0, target->width, target->height);
	draw_set_pipeline(NULL);
	draw_end();
}

void util_layer_update_preview() {
	if (g_context->layers_preview_dirty) {
		g_context->layers_preview_dirty = false;
		g_context->layer_preview_dirty  = false;
		g_context->mask_preview_last    = NULL;
		// Update all layer previews
		for (i32 i = 0; i < g_project->_->layers->length; ++i) {
			slot_layer_t *l = g_project->_->layers->buffer[i];
			if (slot_layer_is_group(l)) {
				continue;
			}

			gpu_texture_t *target = l->texpaint_preview;
			if (target == NULL) {
				continue;
			}

			util_layer_draw_preview(l, target);
		}
		ui_dock_redraw(TAB_AREA_SIDEBAR0);
	}

	if (g_context->layer != NULL && g_context->layer_preview_dirty && !slot_layer_is_group(g_context->layer)) {
		g_context->layer_preview_dirty = false;
		g_context->mask_preview_last   = NULL;
		// Update layer preview
		slot_layer_t  *l      = g_context->layer;
		gpu_texture_t *target = l->texpaint_preview;
		if (target != NULL) {
			util_layer_draw_preview(l, target);
			ui_dock_redraw(TAB_AREA_SIDEBAR0);
		}
	}
}

void layers_init() {
	slot_layer_clear(g_project->_->layers->buffer[0], color_from_floats(layers_default_base, layers_default_base, layers_default_base, 1.0), NULL, 1.0,
	                 layers_default_rough, 0.0);
}

void layers_resize() {
	if (config_get_texture_res_x() >= 8192 || config_get_texture_res_y() >= 8192) { // Save memory for >=8k
		g_config->undo_steps = config_get_texture_res_x() == 8192 ? 4 : 1;
		while (history_undo_layers->length > g_config->undo_steps) {
			slot_layer_t *l = array_pop(history_undo_layers);
			sys_notify_on_next_frame(&slot_layer_unload, l);
		}
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	layers_resize_targets();

	render_path_raytrace_ready = false; // Rebuild baketex
	g_context->ddirty          = 2;
	layers_crop                = false;
}

// Undo layers, live layer and paint targets that follow the texture resolution
void layers_resize_targets() {
	if (history_undo_layers != NULL) {
		for (i32 i = 0; i < history_undo_layers->length; ++i) {
			slot_layer_t *l = history_undo_layers->buffer[i];
			slot_layer_resize_and_set_bits(l);
		}
	}

	any_map_t *rts = render_path_render_targets;

	render_target_t *blend0           = any_map_get(rts, "texpaint_blend0");
	gpu_texture_t   *_texpaint_blend0 = blend0->_image;
	gpu_delete_texture(_texpaint_blend0);
	blend0->width  = config_get_texture_res_x();
	blend0->height = config_get_texture_res_y();
	blend0->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);

	render_target_t *blend1           = any_map_get(rts, "texpaint_blend1");
	gpu_texture_t   *_texpaint_blend1 = blend1->_image;
	gpu_delete_texture(_texpaint_blend1);
	blend1->width  = config_get_texture_res_x();
	blend1->height = config_get_texture_res_y();
	blend1->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);

	g_context->brush_blend_dirty = true;

	render_target_t *blur = any_map_get(rts, "texpaint_blur");
	if (blur != NULL) {
		gpu_texture_t *_texpaint_blur = blur->_image;
		gpu_delete_texture(_texpaint_blur);
		f32 size_x   = math_floor(config_get_texture_res_x() * 0.95);
		f32 size_y   = math_floor(config_get_texture_res_y() * 0.95);
		blur->width  = size_x;
		blur->height = size_y;
		blur->_image = gpu_create_render_target(size_x, size_y, GPU_TEXTURE_FORMAT_RGBA32);
	}
	if (render_path_paint_live_layer != NULL) {
		slot_layer_resize_and_set_bits(render_path_paint_live_layer);
	}

	render_target_t *sculpt_ref = any_map_get(rts, "texpaint_sculpt_ref");
	if (sculpt_ref != NULL) {
		gpu_delete_texture(sculpt_ref->_image);
		sculpt_ref->width  = config_get_texture_res_x();
		sculpt_ref->height = config_get_texture_res_y();
		sculpt_ref->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA128);
	}

	render_target_t *sculpt_base = any_map_get(rts, "texpaint_sculpt_base");
	if (sculpt_base != NULL) {
		gpu_delete_texture(sculpt_base->_image);
		sculpt_base->width  = config_get_texture_res_x();
		sculpt_base->height = config_get_texture_res_y();
		sculpt_base->_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA128);
	}
}

void layers_set_bits() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
	for (i32 i = 0; i < history_undo_layers->length; ++i) {
		slot_layer_t *l = history_undo_layers->buffer[i];
		slot_layer_resize_and_set_bits(l);
	}
}

void layers_make_temp_img() {
	slot_layer_t *l = g_project->_->layers->buffer[0];

	if (layers_temp_image != NULL && (layers_temp_image->width != l->texpaint->width || layers_temp_image->height != l->texpaint->height ||
	                                  layers_temp_image->format != l->texpaint->format)) {
		render_target_t *_temptex0 = any_map_get(render_path_render_targets, "temptex0");
		gpu_delete_texture(_temptex0->_image);
		map_delete(render_path_render_targets, "temptex0");
		layers_temp_image = NULL;
	}

	if (layers_temp_image == NULL) {
		char            *format = base_bits == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
		render_target_t *t      = render_target_create();
		t->name                 = "temptex0";
		t->width                = l->texpaint->width;
		t->height               = l->texpaint->height;
		t->format               = string_copy(format);
		render_target_t *rt     = render_path_create_render_target(t);
		layers_temp_image       = rt->_image;
	}
}

void layers_make_temp_mask_img() {
	if (pipes_temp_mask_image != NULL &&
	    (pipes_temp_mask_image->width != config_get_texture_res_x() || pipes_temp_mask_image->height != config_get_texture_res_y())) {
		gpu_texture_t *_temp_mask_image = pipes_temp_mask_image;
		gpu_delete_texture(_temp_mask_image);
		pipes_temp_mask_image = NULL;
	}

	if (pipes_temp_mask_image == NULL) {
		// pipes_temp_mask_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_R8);
		pipes_temp_mask_image = gpu_create_render_target(config_get_texture_res_x(), config_get_texture_res_y(), GPU_TEXTURE_FORMAT_RGBA32);
	}
}

void layers_make_export_img() {
	slot_layer_t *l = g_project->_->layers->buffer[0];
	if (layers_expa != NULL &&
	    (layers_expa->width != l->texpaint->width || layers_expa->height != l->texpaint->height || layers_expa->format != l->texpaint->format)) {
		gpu_texture_t *_expa = layers_expa;
		gpu_texture_t *_expb = layers_expb;
		gpu_texture_t *_expc = layers_expc;
		gpu_delete_texture(_expa);
		gpu_delete_texture(_expb);
		gpu_delete_texture(_expc);
		layers_expa = NULL;
		layers_expb = NULL;
		layers_expc = NULL;
		map_delete(render_path_render_targets, "expa");
		map_delete(render_path_render_targets, "expb");
		map_delete(render_path_render_targets, "expc");
	}
	if (layers_expa == NULL) {
		char *format = base_bits == TEXTURE_BITS_BITS8 ? "RGBA32" : base_bits == TEXTURE_BITS_BITS16 ? "RGBA64" : "RGBA128";
		{
			render_target_t *t  = render_target_create();
			t->name             = "expa";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			layers_expa         = rt->_image;
		}
		{
			render_target_t *t  = render_target_create();
			t->name             = "expb";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			layers_expb         = rt->_image;
		}
		{
			render_target_t *t  = render_target_create();
			t->name             = "expc";
			t->width            = l->texpaint->width;
			t->height           = l->texpaint->height;
			t->format           = string_copy(format);
			render_target_t *rt = render_path_create_render_target(t);
			layers_expc         = rt->_image;
		}
	}
}

// Merges occlusion, roughness, metallic and height of l1 into i0, each with its channel blending and opacity,
// layers_temp_image holds a copy of i0
void layers_commands_merge_pack(gpu_texture_t *i0, slot_layer_t *l1, gpu_texture_t *i1texmask) {
	_gpu_begin(i0, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
	gpu_set_pipeline(pipes_merge);
	gpu_set_texture(pipes_tex0, l1->texpaint);
	gpu_set_texture(pipes_tex1, l1->texpaint_pack);
	gpu_set_texture(pipes_texmask, i1texmask);
	gpu_set_texture(pipes_texa, layers_temp_image);
	gpu_set_float(pipes_opac, 1.0);
	gpu_set_float(pipes_tex1w, l1->texpaint_pack->width);
	gpu_set_int(pipes_blending, 105);
	gpu_set_float4(pipes_opac_ch, l1->paint_occ ? slot_layer_get_channel_opacity(l1, LAYER_CH_OCC) : 0.0,
	               l1->paint_rough ? slot_layer_get_channel_opacity(l1, LAYER_CH_ROUGH) : 0.0,
	               l1->paint_met ? slot_layer_get_channel_opacity(l1, LAYER_CH_MET) : 0.0,
	               l1->paint_height ? slot_layer_get_channel_opacity(l1, LAYER_CH_HEIGHT) : 0.0);
	gpu_set_float4(pipes_blend_ch, slot_layer_get_blending(l1, LAYER_CH_OCC), slot_layer_get_blending(l1, LAYER_CH_ROUGH),
	               slot_layer_get_blending(l1, LAYER_CH_MET), slot_layer_get_blending(l1, LAYER_CH_HEIGHT));
	gpu_set_vertex_buffer(const_data_screen_aligned_vb);
	gpu_set_index_buffer(const_data_screen_aligned_ib);
	gpu_draw();
	gpu_end();
}

// Blending mode of the layer_merge shader for the normal channel
i32 layers_nor_merge_mode(slot_layer_t *l1) {
	return slot_layer_get_blending(l1, LAYER_CH_NOR) == BLEND_TYPE_NORMAL_COMBINE ? 102 : 101;
}

bool layers_is_fill_material() {
	if (g_context->tool == TOOL_TYPE_MATERIAL) {
		return true;
	}

	slot_material_t *m = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill_material == m) {
			return true;
		}
	}
	return false;
}

static void layers_update_fill_layers_material();
static void layers_refill(bool (*selected)(slot_layer_t *l, void *data), void *data);
static bool layers_refill_effect_of_material(slot_layer_t *l, void *data);

// Fill layers and masks of the current material, then the effects that may read them
void layers_update_fill_layers() {
	layers_update_fill_layers_material();
	layers_refill(&layers_refill_effect_of_material, g_context->material);
}

// Points the Effect Input node of an effect material to the layer or mask it reads
static void layers_set_effect_input(slot_material_t *m, slot_layer_t *input) {
	i32 index = array_index_of(g_project->_->layers, input);
	for (i32 i = 0; i < m->canvas->nodes->length; ++i) {
		ui_node_t *n = m->canvas->nodes->buffer[i];
		if ((string_equals(n->type, "LAYER") || string_equals(n->type, "LAYER_MASK")) && string_equals(n->name, "Effect Input")) {
			n->buttons->buffer[0]->default_value->buffer[0] = index;
		}
	}
}

// The layer an effect or mask belongs to, the layer itself otherwise
static slot_layer_t *layers_owner(slot_layer_t *l) {
	return l != NULL && l->parent != NULL && (slot_layer_is_mask(l) || slot_layer_is_filter(l)) ? l->parent : l;
}

// The material reads layers through Layer or Layer Mask nodes, other than the input of an effect
static bool layers_material_linked(slot_material_t *m) {
	for (i32 i = 0; m != NULL && i < m->canvas->nodes->length; ++i) {
		ui_node_t *n = m->canvas->nodes->buffer[i];
		if ((string_equals(n->type, "LAYER") || string_equals(n->type, "LAYER_MASK")) && !string_equals(n->name, "Effect Input")) {
			return true;
		}
	}
	return false;
}

// Refills the slots that pass the filter in one pass from the bottom of the stack up, so each one reads fresh layers
// below it: effects read the element below them, linked fills read other layers through their nodes
static void layers_refill(bool (*selected)(slot_layer_t *l, void *data), void *data) {
	if (bake_texture_node_baking) {
		return;
	}
	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	bool             any       = false;
	bool             any_fill  = false; // Changed the current layer and material
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_paint_effect(l)) { // No material, its result is rebuilt from its input and its strokes
			if (selected(l, data)) {
				layers_update_paint_effect(l);
				any = true;
			}
			continue;
		}
		if (l->fill_material == NULL || l->texpaint_sculpt != NULL || slot_layer_is_group(l) || !selected(l, data)) {
			continue;
		}
		if (slot_layer_is_effect(l)) {
			slot_layer_t *base = slot_layer_effect_base(l);
			if (base == NULL || array_index_of(g_project->_->layers, base) < 0) {
				continue; // Lost its layer or mask, keeps its last result
			}
			slot_layer_t *input = slot_layer_effect_input(l);
			if (input != NULL) {
				layers_set_effect_input(l->fill_material, input);
			}
		}
		g_context->layer    = l;
		g_context->material = l->fill_material;
		layers_set_object_mask();
		layers_update_fill_layer(true);
		any      = true;
		any_fill = true;
	}
	if (!any) {
		return;
	}
	if (any_fill) {
		g_context->layer    = _layer;
		g_context->material = _material;
		layers_set_object_mask();
		make_material_parse_paint_material(false);
	}
	g_context->rtdirty              = 1;
	g_context->ddirty               = 2;
	g_context->layers_preview_dirty = true;
}

static bool layers_refill_effect(slot_layer_t *l, void *data) {
	return slot_layer_is_effect(l);
}

// Effects of the layers that own a slot filled with the material, and effects reading other layers
static bool layers_refill_effect_of_material(slot_layer_t *l, void *data) {
	if (!slot_layer_is_effect(l)) {
		return false;
	}
	if (l->fill_material == data || layers_material_linked(l->fill_material)) {
		return true;
	}
	slot_layer_t *owner = layers_owner(l);
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *f = g_project->_->layers->buffer[i];
		if (f->fill_material == data && layers_owner(f) == owner) {
			return true;
		}
	}
	return false;
}

// After painting: effects of the painted layer, and every fill reading other layers
static bool layers_refill_after_paint(slot_layer_t *l, void *data) {
	if (layers_material_linked(l->fill_material)) {
		return true;
	}
	return slot_layer_is_effect(l) && layers_owner(l) == data;
}

// Effects are refilled from the bottom of the stack up, so each one reads the fresh output of the one below it
void layers_update_effects() {
	layers_refill(&layers_refill_effect, NULL);
}

// Effects of the stacks of the layer of l and fills reading other layers, after a change only they show
void layers_update_effects_of(slot_layer_t *l) {
	layers_refill(&layers_refill_after_paint, layers_owner(l));
}

bool layers_has_effects() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *e = g_project->_->layers->buffer[i];
		if (slot_layer_is_effect(e) && (e->fill_material != NULL || e->effect_paint)) {
			return true;
		}
	}
	return false;
}

// Paint effect: the element below it in its stack with the strokes merged over it like a layer merged down, each channel
// with the blending and opacity of the effect. A mask paint effect blends the value of its strokes by their coverage.
void layers_update_paint_effect(slot_layer_t *e) {
	slot_layer_t *base = slot_layer_effect_base(e);
	if (base == NULL || array_index_of(g_project->_->layers, base) < 0 || e->texpaint_result == NULL) {
		return; // Lost its layer or mask, keeps its last result
	}
	slot_layer_t  *in      = slot_layer_effect_input(e);
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use) {
		draw_end();
	}

	bool            mask = slot_layer_is_mask(e);
	gpu_pipeline_t *pipe = mask || base_bits == TEXTURE_BITS_BITS8 ? pipes_copy : base_bits == TEXTURE_BITS_BITS16 ? pipes_copy64 : pipes_copy128;
	gpu_texture_t  *to[] = {e->texpaint_result, e->texpaint_result_nor, e->texpaint_result_pack};
	for (i32 i = 0; i < 3 && to[i] != NULL; ++i) {
		draw_begin(to[i], false, 0);
		draw_set_pipeline(pipe);
		draw_image(slot_layer_result(in, i), 0, 0); // The input can be a lower paint effect
		draw_set_pipeline(NULL);
		draw_end();
	}

	slot_layer_t s = *e; // The strokes, merged like a layer of their own
	s.parent       = NULL;
	s.effect       = false;
	s.effect_paint = false;
	s.visible      = true;
	s.mask_opacity = 1.0;
	s.paint_base   = true;
	s.paint_nor    = !mask;
	s.paint_occ    = !mask;
	s.paint_rough  = !mask;
	s.paint_met    = !mask;
	s.paint_height = !mask;
	if (mask) {
		s.texpaint_nor = e->texpaint; // Not read, merges the strokes like a layer: the value by its coverage in alpha
	}
	slot_layer_t d = {.texpaint = e->texpaint_result, .texpaint_nor = e->texpaint_result_nor, .texpaint_pack = e->texpaint_result_pack};
	layers_merge_layer(&d, &s, false);

	if (in_use) {
		draw_begin(current, false, 0);
	}
}

// Every paint effect, from the bottom of the stack up, after a project is opened
void layers_update_paint_effects() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_paint_effect(l)) {
			layers_update_paint_effect(l);
		}
	}
}

// A paint effect becomes a plain layer or mask holding its result, used before destructive merges
void layers_bake_paint_effect(slot_layer_t *e) {
	gpu_texture_t *from[] = {e->texpaint_result, e->texpaint_result_nor, e->texpaint_result_pack};
	gpu_texture_t *to[]   = {e->texpaint, e->texpaint_nor, e->texpaint_pack};
	for (i32 i = 0; i < 3 && from[i] != NULL && to[i] != NULL; ++i) {
		draw_begin(to[i], false, 0);
		draw_set_pipeline(pipes_copy);
		draw_image(from[i], 0, 0);
		draw_set_pipeline(NULL);
		draw_end();
	}
	slot_layer_free_result(e);
	e->effect_paint = false;
}

void layers_update_effects_on_next_frame(void *_) {
	layers_update_effects();
}

static void layers_update_fill_layers_material() {
	if (bake_texture_node_baking)
		return;

	slot_layer_t  *_layer     = g_context->layer;
	tool_type_t    _tool      = g_context->tool;
	i32            _fill_type = g_context->fill_type;
	gpu_texture_t *current    = NULL;

	if (g_context->tool == TOOL_TYPE_MATERIAL) {
		if (render_path_paint_live_layer == NULL) {
			render_path_paint_live_layer = slot_layer_create("_live", LAYER_SLOT_TYPE_LAYER, NULL);
		}

		current     = _draw_current;
		bool in_use = gpu_in_use;
		if (in_use)
			draw_end();

		g_context->tool      = TOOL_TYPE_FILL;
		g_context->fill_type = FILL_TYPE_OBJECT;
		render_path_paint_set_plane_mesh();
		make_material_parse_paint_material(false);
		g_context->pdirty = 1;
		render_path_paint_use_live_layer(true);
		render_path_paint_commands_paint(false);
		render_path_paint_dilate(true, true);
		render_path_paint_use_live_layer(false);
		g_context->tool      = _tool;
		g_context->fill_type = _fill_type;
		g_context->pdirty    = 0;
		render_path_paint_restore_plane_mesh();
		make_material_parse_paint_material(true);
		ui_view2d_hwnd->redraws = 2;

		if (in_use)
			draw_begin(current, false, 0);
		return;
	}

	bool has_fill_layer = false;
	bool has_fill_mask  = false;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_layer(l) && l->fill_material == g_context->material && !slot_layer_is_effect(l)) {
			has_fill_layer = true;
		}
	}
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_mask(l) && l->fill_material == g_context->material && !slot_layer_is_effect(l)) {
			has_fill_mask = true;
		}
	}

	if (has_fill_layer || has_fill_mask) {
		current     = _draw_current;
		bool in_use = gpu_in_use;
		if (in_use)
			draw_end();
		g_context->pdirty    = 1;
		g_context->tool      = TOOL_TYPE_FILL;
		g_context->fill_type = FILL_TYPE_OBJECT;

		if (has_fill_layer) {
			bool first = true;
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (slot_layer_is_layer(l) && l->fill_material == g_context->material && !slot_layer_is_effect(l)) {
					g_context->layer = l;
					if (first) {
						first = false;
						make_material_parse_paint_material(false);
					}
					layers_set_object_mask();
					if (l->texpaint_sculpt != NULL) {
						i32 tid = l->id;
						i32 hid = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
						sculpt_import_mesh_pack_to_texture(l->texpaint_sculpt);
						render_path_set_target(string_tmp("texpaint_sculpt_undo%d", hid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
						render_path_bind_target(string("texpaint_sculpt%d", tid), "tex");
						render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");
						render_path_sculpt_commands();
					}
					else {
						slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
						render_path_paint_commands_paint(false);
						render_path_paint_dilate(true, true);
					}
				}
			}
		}
		if (has_fill_mask) {
			bool first = true;
			for (i32 i = 0; i < g_project->_->layers->length; ++i) {
				slot_layer_t *l = g_project->_->layers->buffer[i];
				if (slot_layer_is_mask(l) && l->fill_material == g_context->material && !slot_layer_is_effect(l)) {
					g_context->layer = l;
					if (first) {
						first = false;
						make_material_parse_paint_material(false);
					}
					layers_set_object_mask();
					slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
					render_path_paint_commands_paint(false);
					render_path_paint_dilate(true, true);
				}
			}
		}
		g_context->pdirty               = 0;
		g_context->ddirty               = 2;
		g_context->rtdirty              = 1;
		g_context->layers_preview_dirty = true;
		if (in_use)
			draw_begin(current, false, 0);
		g_context->layer = _layer;
		layers_set_object_mask();
		g_context->tool      = _tool;
		g_context->fill_type = _fill_type;
		make_material_parse_paint_material(false);
	}
}

bool layers_is_path_material() {
	slot_material_t *m = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_path(l) && l->path_material == m) {
			return true;
		}
	}
	return false;
}

void layers_update_path_layers() {
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (slot_layer_is_path(l) && l->path_material == g_context->material) {
			util_layer_repaint_path(l);
		}
	}
}

bool layers_tool_paints_fill_layer(tool_type_t tool) {
	return tool == TOOL_TYPE_BAKE || tool == TOOL_TYPE_PICKER || tool == TOOL_TYPE_MATERIAL || tool == TOOL_TYPE_COLORID;
}

void layers_update_fill_layer(bool parse_paint) {
	gpu_texture_t *current = _draw_current;
	bool           in_use  = gpu_in_use;
	if (in_use)
		draw_end();

	tool_type_t _tool      = g_context->tool;
	i32         _fill_type = g_context->fill_type;
	g_context->tool        = TOOL_TYPE_FILL;
	g_context->fill_type   = FILL_TYPE_OBJECT;
	g_context->pdirty      = 1;

	if (g_context->layer->texpaint_sculpt != NULL) {
		i32 tid = g_context->layer->id;
		i32 hid = history_undo_i - 1 < 0 ? g_config->undo_steps - 1 : history_undo_i - 1;
		sculpt_import_mesh_pack_to_texture(g_context->layer->texpaint_sculpt);
		render_path_set_target(string_tmp("texpaint_sculpt_undo%d", hid), NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		render_path_bind_target(string("texpaint_sculpt%d", tid), "tex");
		render_path_draw_shader("Scene/copy_pass/copyRGBA128_pass");
		if (parse_paint) {
			make_material_parse_paint_material(false);
		}
		render_path_sculpt_commands();
	}
	else {
		slot_layer_clear(g_context->layer, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
		if (parse_paint) {
			make_material_parse_paint_material(false);
		}
		render_path_paint_commands_paint(false);
		render_path_paint_dilate(true, true);
	}

	g_context->tool      = _tool;
	g_context->fill_type = _fill_type;
	if (parse_paint && layers_tool_paints_fill_layer(_tool)) {
		make_material_parse_paint_material(false);
	}
	if (in_use)
		draw_begin(current, false, 0);
}

// Runs every frame of a stroke: fills reading other layers and the effects of the painted layer, in one pass bottom up
void layers_update_linked_layers() {
	nodes_mesh_layer_changed(g_context->layer);
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		if (layers_material_linked(m)) {
			g_context->material = m;
			make_material_bake_node_previews_linked();
		}
	}
	g_context->material = _material;
	layers_refill(&layers_refill_after_paint, layers_owner(g_context->layer));
}

void layers_set_object_mask() {
	string_array_t *ar = any_array_create_from_raw(
	    (void *[]){
	        tr("None"),
	    },
	    1);
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		any_array_push(ar, p->base->name);
	}

	i32 mask = context_object_mask_used() ? slot_layer_get_object_mask(g_context->layer) : 0;
	if (context_layer_filter_used()) {
		mask = g_context->layer_filter;
	}
	if (mask > 0) {
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = false;
		}
		mesh_object_t *o = g_project->_->paint_objects->buffer[0];
		for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
			mesh_object_t *p         = g_project->_->paint_objects->buffer[i];
			char          *mask_name = ar->buffer[mask];
			if (string_equals(p->base->name, mask_name)) {
				o = p;
				break;
			}
		}
		context_select_paint_object(o);
	}
	else {
		i32 atlas_mask = g_context->layer_filter > g_project->_->paint_objects->length ? g_context->layer_filter : slot_layer_get_object_mask(g_context->layer);
		bool is_atlas  = atlas_mask > g_project->_->paint_objects->length;
		if (g_context->merged_object == NULL || is_atlas || g_context->merged_object_is_atlas) {
			mesh_object_t_array_t *visibles = is_atlas ? project_get_atlas_objects(atlas_mask) : NULL;
			util_mesh_merge(visibles);
		}
		context_select_paint_object(context_main_object());
		if (g_context->merged_object != NULL) {
			g_context->merged_object->base->visible = true;
		}
	}
	util_uv_dilatemap_cached = false;
	util_geometry_mask_invalidate();
}

void tab_layers_apply_filter(i32 filter) {
	g_context->layer_filter = filter;
	char *filter_name       = NULL;
	if (filter > 0 && filter <= g_project->_->paint_objects->length) {
		filter_name = g_project->_->paint_objects->buffer[filter - 1]->base->name;
	}
	else if (filter > g_project->_->paint_objects->length) {
		string_array_t *atlases = project_get_used_atlases();
		if (atlases != NULL) {
			filter_name = atlases->buffer[filter - g_project->_->paint_objects->length - 1];
		}
	}
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		mesh_object_t *p = g_project->_->paint_objects->buffer[i];
		p->base->visible = filter == 0 || (filter_name != NULL && string_equals(p->base->name, filter_name)) || project_is_atlas_object(p);
	}
	if (filter == 0 && g_context->merged_object_is_atlas) {
		util_mesh_merge(NULL);
	}
	else if (filter > g_project->_->paint_objects->length) {
		util_mesh_merge(util_mesh_get_visible());
	}
	layers_set_object_mask();
	util_uv_uvmap_cached       = false;
	g_context->ddirty          = 2;
	render_path_raytrace_ready = false;
}

void layers_new_layer_clear(slot_layer_t *l) {
	if (array_index_of(g_project->_->layers, l) < 0) {
		return;
	}
	if (slot_layer_is_path(l) && l->path_points->length > 0) {
		return;
	}
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *layers_new_layer(bool clear, i32 position, slot_layer_t *parent) {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_LAYER, parent);
	l->object_mask  = g_context->layer_filter;

	if (position == -1 && slot_layer_is_filter(l)) {
		position = array_index_of(g_project->_->layers, parent);
	}

	if (position == -1) {
		if (slot_layer_is_mask(g_context->layer) || slot_layer_is_filter(g_context->layer))
			context_set_layer(g_context->layer->parent);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, g_context->layer) + 1, l);
	}
	else {
		array_insert(g_project->_->layers, position, l);
	}

	context_set_layer(l);

	if (parent == NULL) {
		i32 li = array_index_of(g_project->_->layers, g_context->layer);
		if (li > 0) {
			slot_layer_t *below = g_project->_->layers->buffer[li - 1];
			if (slot_layer_is_layer(below)) {
				g_context->layer->parent = below->parent;
			}
		}
	}

	if (clear) {
		sys_notify_on_next_frame(&layers_new_layer_clear, l);
	}

	tab_stages_add_layer(l->name);

	g_context->layer_preview_dirty = true;
	return l;
}

void layers_new_mask_clear(slot_layer_t *l) {
	slot_layer_clear(l, 0x00000000, NULL, 1.0, layers_default_rough, 0.0);
}

slot_layer_t *layers_new_mask(bool clear, slot_layer_t *parent, i32 position) {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_MASK, parent);
	if (position == -1) {
		position = array_index_of(g_project->_->layers, parent);
	}
	array_insert(g_project->_->layers, position, l);
	context_set_layer(l);
	if (clear) {
		sys_notify_on_next_frame(&layers_new_mask_clear, l);
	}
	tab_stages_add_layer(l->name);
	g_context->layer_preview_dirty = true;
	return l;
}

slot_layer_t *layers_new_group() {
	if (g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}

	slot_layer_t *l = slot_layer_create("", LAYER_SLOT_TYPE_GROUP, NULL);
	any_array_push(g_project->_->layers, l);
	context_set_layer(l);
	tab_stages_add_layer(l->name);
	return l;
}

static slot_layer_t *_layers_path_sculpt_layer = NULL;

void layers_new_path_layer_sculpt_init(void *_) {
	sculpt_init();
	sculpt_init_sculpt_texture(_layers_path_sculpt_layer);
	make_material_parse_mesh_material();
}

slot_layer_t *layers_new_path_layer(bool curved) {
	slot_layer_t *l = layers_new_layer(true, -1, NULL);
	if (l == NULL) {
		return NULL;
	}
	l->path_points        = f32_array_create(0);
	l->path_points_world  = f32_array_create(0);
	l->path_points_camera = f32_array_create(0);
	l->path_points_parent = i32_array_create(0);
	l->path_tool          = -1;
	l->path_curved        = curved;
	l->path_material      = g_context->material;
	char *name            = string_copy(slot_layer_unique_name(l, string_tmp(curved ? "Curve %d" : "Path %d", l->id + 1)));
	tab_stages_rename_layer(l->name, name);
	l->name = name;

	if (g_config->tools == TOOLS_SCULPT) {
		_layers_path_sculpt_layer = l;
		sys_notify_on_next_frame(&layers_new_path_layer_sculpt_init, NULL);
	}

	return l;
}

slot_layer_t *layers_new_text_layer() {
	slot_layer_t *l = layers_new_path_layer(true);
	if (l == NULL) {
		return NULL;
	}
	l->path_text = true;
	l->path_tool = TOOL_TYPE_TEXT;
	char *name   = string_copy(slot_layer_unique_name(l, string_tmp("Text %d", l->id + 1)));
	tab_stages_rename_layer(l->name, name);
	l->name = name;
	return l;
}

void layers_create_fill_layer_on_next_frame(void *_) {
	slot_layer_t *l = layers_new_layer(false, _layers_position, NULL);
	history_new_layer();
	l->uv_type = _layers_uv_type;
	if (!mat4_isnan(_layers_decal_mat)) {
		l->decal_mat = _layers_decal_mat;
	}
	l->object_mask = g_context->layer_filter;

	if (g_config->tools == TOOLS_SCULPT) {
		sculpt_init();
		sculpt_init_sculpt_texture(l);
	}

	history_to_fill_layer();
	slot_layer_to_fill_layer(l);
}

void layers_create_fill_layer(uv_type_t uv_type, mat4_t decal_mat, i32 position) {
	_layers_uv_type   = uv_type;
	_layers_decal_mat = decal_mat;
	_layers_position  = position;
	sys_notify_on_next_frame(&layers_create_fill_layer_on_next_frame, NULL);
}

void tab_materials_button_new_on_next_frame(void *_);

void layers_create_filter_on_next_frame(void *_) {
	if (slot_layer_get_filters(g_context->layer, true) != NULL) {
		return;
	}

	tab_materials_button_new_on_next_frame(NULL);

	slot_layer_t *l = layers_new_layer(false, -1, g_context->layer);
	history_new_layer();
	history_to_fill_layer();
	slot_layer_to_fill_layer(l);

	// Filter material
	g_context->material->canvas->name = string_copy(g_context->layer->name);

	ui_nodes_t       *nodes  = g_context->material->nodes;
	ui_node_canvas_t *canvas = g_context->material->canvas;
	ui_node_t        *nout   = NULL;
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			nout = n;
			break;
		}
	}
	for (i32 i = 0; i < canvas->nodes->length; ++i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->name, "Color")) {
			ui_remove_node(n, canvas);
			break;
		}
	}

	ui_node_t *n                         = nodes_material_create_node("LAYER", NULL);
	n->buttons->buffer[0]->default_value = f32_array_create_x(array_index_of(g_project->_->layers, l->parent));
	n->x                                 = -50;
	n->y                                 = 100;

	for (int i = 0; i < 9; ++i) {
		ui_node_link_t *l =
		    ALLOC_INIT(ui_node_link_t, {.id = ui_next_link_id(canvas->links), .from_id = n->id, .from_socket = i, .to_id = nout->id, .to_socket = i});
		any_array_push(canvas->links, l);
	}

	layers_update_fill_layer(true);
	util_render_make_material_preview();
}

void layers_create_filter() {
	sys_notify_on_next_frame(&layers_create_filter_on_next_frame, NULL);
}

char *layers_effect_name(effect_type_t type) {
	switch (type) {
	case EFFECT_TYPE_FILL:
		return tr("Fill");
	case EFFECT_TYPE_LEVELS:
		return tr("Levels");
	case EFFECT_TYPE_HSV:
		return tr("Hue/Saturation/Value");
	case EFFECT_TYPE_COLOR_BALANCE:
		return tr("Color Balance");
	case EFFECT_TYPE_SHARPEN:
		return tr("Sharpen");
	case EFFECT_TYPE_BLUR:
		return tr("Blur");
	case EFFECT_TYPE_COMPARE_MASK:
		return tr("Compare Mask");
	case EFFECT_TYPE_COLOR_SELECTION:
		return tr("Color Selection");
	case EFFECT_TYPE_ANCHOR:
		return tr("Anchor Point");
	case EFFECT_TYPE_PAINT:
		return tr("Paint");
	case EFFECT_TYPE_GENERATOR:
		return tr("Generator");
	default:
		return tr("Filter");
	}
}

char *layers_generator_name(generator_type_t g) {
	switch (g) {
	case GENERATOR_TYPE_GRADIENT:
		return tr("3D Gradient");
	case GENERATOR_TYPE_FACING_UP:
		return tr("Facing Up");
	case GENERATOR_TYPE_NOISE:
		return tr("Noise");
	default:
		return tr("Edges");
	}
}

// Color effects are for layers, mask generators for masks. Paint and Generator fit both.
bool layers_effect_allowed(effect_type_t type, bool mask) {
	if (type < 0 || type >= EFFECT_TYPE_COUNT) {
		return false;
	}
	if (type == EFFECT_TYPE_HSV || type == EFFECT_TYPE_COLOR_BALANCE) {
		return !mask;
	}
	if (type == EFFECT_TYPE_COMPARE_MASK || type == EFFECT_TYPE_COLOR_SELECTION) {
		return mask;
	}
	return true;
}

static ui_node_t *layers_effect_add_node(slot_material_t *m, char *type, f32 x, f32 y) {
	ui_node_t *n = ui_nodes_make_node(nodes_material_get_node_t(type), m->nodes, m->canvas);
	n->x         = x;
	n->y         = y;
	any_array_push(m->canvas->nodes, n);
	return n;
}

// Node chain of a generator preset ending in a clamped Map Range at x, y, returns the Map Range
static ui_node_t *layers_effect_add_generator(slot_material_t *m, generator_type_t g, f32 x, f32 y) {
	ui_node_canvas_t *canvas                            = m->canvas;
	ui_node_t        *range                             = layers_effect_add_node(m, "MAPRANGE", x, y);
	ui_node_t        *src                               = NULL;
	i32               socket                            = 0;
	f32               from_min                          = 0.0;
	f32               from_max                          = 1.0;
	range->buttons->buffer[0]->default_value->buffer[0] = 1.0; // Clamp
	if (g == GENERATOR_TYPE_GRADIENT || g == GENERATOR_TYPE_FACING_UP) {
		// Z of the world position or of the world normal, z is up
		ui_node_t *geometry = layers_effect_add_node(m, "NEW_GEOMETRY", x - 500, y);
		src                 = layers_effect_add_node(m, "SEPXYZ", x - 250, y);
		any_array_push(canvas->links, project_create_node_link(canvas->links, geometry->id, g == GENERATOR_TYPE_GRADIENT ? 0 : 1, src->id, 0));
		socket   = 2;
		from_min = g == GENERATOR_TYPE_GRADIENT ? -1.0 : 0.5;
	}
	else if (g == GENERATOR_TYPE_NOISE) {
		src                                              = layers_effect_add_node(m, "TEX_NOISE", x - 250, y);
		src->inputs->buffer[1]->default_value->buffer[0] = 8.0; // Scale
		src->inputs->buffer[2]->default_value->buffer[0] = 4.0; // Detail
		from_min                                         = 0.4;
		from_max                                         = 0.6;
	}
	else { // Edges: curvature baked in uv space, Strength 1, Radius 1 and Offset 0
		src      = layers_effect_add_node(m, "BAKE_CURVATURE", x - 250, y);
		from_min = 0.2;
		from_max = 0.5;
	}
	range->inputs->buffer[1]->default_value->buffer[0] = from_min;
	range->inputs->buffer[2]->default_value->buffer[0] = from_max;
	any_array_push(canvas->links, project_create_node_link(canvas->links, src->id, socket, range->id, 0));
	return range;
}

// Effect Input (the layer or mask below) -> effect node -> Material Output, the other channels of a layer pass through.
// A generator is a fill whose factor is the node chain of the preset gen.
static slot_material_t *layers_new_effect_material(char *name, bool mask, effect_type_t type, generator_type_t gen) {
	slot_material_t  *m      = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	ui_node_canvas_t *canvas = m->canvas;
	canvas->name             = string_copy(slot_material_unique_name(canvas, name));
	ui_node_t *output        = NULL;
	for (i32 i = canvas->nodes->length - 1; i >= 0; --i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			output = n;
		}
		else {
			array_remove(canvas->nodes, n);
		}
	}
	canvas->links->length = 0;
	nodes_material_init();
	ui_node_t *input = layers_effect_add_node(m, mask ? "LAYER_MASK" : "LAYER", output->x - 500, output->y);
	input->name      = "Effect Input";

	bool  fill      = type == EFFECT_TYPE_FILL || type == EFFECT_TYPE_GENERATOR;
	char *node_type = fill                                  ? "MIX_RGB"
	                  : type == EFFECT_TYPE_LEVELS          ? "LEVELS"
	                  : type == EFFECT_TYPE_HSV             ? "HUE_SAT"
	                  : type == EFFECT_TYPE_COLOR_BALANCE   ? "COLOR_BALANCE"
	                  : type == EFFECT_TYPE_SHARPEN         ? "SHARPEN"
	                  : type == EFFECT_TYPE_BLUR            ? "BLUR"
	                  : type == EFFECT_TYPE_COMPARE_MASK    ? "COMPARE_MASK"
	                  : type == EFFECT_TYPE_COLOR_SELECTION ? "COLOR_SELECTION"
	                                                        : NULL;
	i32   from_id   = input->id;
	if (node_type != NULL) {
		ui_node_t *e      = layers_effect_add_node(m, node_type, output->x - 250, output->y);
		i32        socket = fill ? 1 : type == EFFECT_TYPE_HSV ? 4 : 0; // Color 1 of Mix, Color of HSV
		if (fill) {
			e->inputs->buffer[0]->default_value->buffer[0] = 1.0; // Factor
			f32_array_t *color                             = e->inputs->buffer[2]->default_value;
			color->buffer[0]                               = 1.0; // White, a fill starts fully visible in a mask
			color->buffer[1]                               = 1.0;
			color->buffer[2]                               = 1.0;
		}
		if (type == EFFECT_TYPE_GENERATOR) { // The generator drives the factor
			ui_node_t *g = layers_effect_add_generator(m, gen, e->x - 250, e->y + 200);
			any_array_push(canvas->links, project_create_node_link(canvas->links, g->id, 0, e->id, 0));
		}
		if (type == EFFECT_TYPE_COMPARE_MASK) {
			e->inputs->buffer[1]->default_value->buffer[0] = 0.5; // B
		}
		if (type != EFFECT_TYPE_COLOR_SELECTION) { // Selection reads an id map linked by the user
			any_array_push(canvas->links, project_create_node_link(canvas->links, input->id, 0, e->id, socket));
		}
		from_id = e->id;
	}
	any_array_push(canvas->links, project_create_node_link(canvas->links, from_id, 0, output->id, 0));
	if (!mask) {
		for (i32 i = 1; i < input->outputs->length && i < output->inputs->length; ++i) {
			any_array_push(canvas->links, project_create_node_link(canvas->links, input->id, i, output->id, i));
		}
	}

	any_array_push(g_project->_->materials, m);
	g_context->material = m;
	tab_materials_update_material();
	history_new_material();
	return m;
}

static slot_layer_t *layers_add_effect_slot(slot_layer_t *target, slot_material_t *m, char *name);

// The layer or mask whose effect stack takes a new effect, the base of an effect, NULL when it takes none
static slot_layer_t *layers_effect_target(slot_layer_t *target) {
	if (target == NULL || slot_layer_is_group(target) || target->texpaint_sculpt != NULL || g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}
	if (slot_layer_is_effect(target) && slot_layer_effect_base(target) != NULL) {
		return slot_layer_effect_base(target);
	}
	return target;
}

// New effect on top of the effect chain of a layer or mask, each effect has its own material but the Paint effect
slot_layer_t *layers_add_effect(slot_layer_t *target, effect_type_t type) {
	target = layers_effect_target(target);
	if (target == NULL || !layers_effect_allowed(type, slot_layer_is_mask(target))) {
		return NULL;
	}
	if (type == EFFECT_TYPE_PAINT) {
		return layers_add_paint_effect(target, NULL);
	}
	if (type == EFFECT_TYPE_GENERATOR) {
		return layers_add_generator(target, GENERATOR_TYPE_EDGES);
	}
	char            *name = layers_effect_name(type);
	slot_material_t *m    = layers_new_effect_material(string_tmp("%s %s", target->name, name), slot_layer_is_mask(target), type, GENERATOR_TYPE_EDGES);
	return layers_add_effect_slot(target, m, name);
}

// New Generator effect with the preset g, its parameters are edited in the node editor
slot_layer_t *layers_add_generator(slot_layer_t *target, generator_type_t g) {
	target = layers_effect_target(target);
	if (target == NULL || g < 0 || g >= GENERATOR_TYPE_COUNT) {
		return NULL;
	}
	char            *name = layers_generator_name(g);
	slot_material_t *m    = layers_new_effect_material(string_tmp("%s %s", target->name, name), slot_layer_is_mask(target), EFFECT_TYPE_GENERATOR, g);
	return layers_add_effect_slot(target, m, name);
}

// Empty layer or mask on top of the effect stack of target, named name, the current layer
static slot_layer_t *layers_new_effect_slot(slot_layer_t *target, char *name) {
	bool                  mask     = slot_layer_is_mask(target);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *e        = NULL;
	context_set_layer(target);
	if (mask) {
		i32 top = array_index_of(g_project->_->layers, target);
		while (top + 1 < g_project->_->layers->length) {
			slot_layer_t *l = g_project->_->layers->buffer[top + 1];
			if (!slot_layer_is_mask(l) || l->parent != target->parent || !l->effect) {
				break;
			}
			top++;
		}
		e = layers_new_mask(false, target->parent, top + 1);
		if (e != NULL) {
			e->effect = true;
		}
	}
	else {
		e = layers_new_layer(false, -1, target); // Right below the layer, the top of its filters
	}
	if (e == NULL) {
		return NULL;
	}
	tab_layers_remap_all_layer_pointers(pointers);
	char *old_name = e->name;
	e->name        = string_copy(slot_layer_unique_name(e, name));
	tab_stages_rename_layer(old_name, e->name); // The layers tab lists the names of the stage
	return e;
}

// The layer or mask of an effect with the material m on top of the effect stack of target
static slot_layer_t *layers_add_effect_slot(slot_layer_t *target, slot_material_t *m, char *name) {
	slot_layer_t *e = layers_new_effect_slot(target, name);
	if (e == NULL) {
		return NULL;
	}
	if (slot_layer_is_mask(e)) {
		history_new_fill_mask();
		history_keep_layer_props(e);
	}
	else {
		history_new_layer();
		history_keep_layer_props(e);
		history_to_fill_layer();
	}
	layers_set_effect_input(m, slot_layer_effect_input(e));
	g_context->material = m;
	slot_layer_to_fill_layer(e);
	make_material_parse_mesh_material();
	g_context->layers_preview_dirty = true;
	return e;
}

// New Paint effect on top of the effect stack of target, strokes painted over what is below it in the stack. src is a
// paint effect whose strokes, blending and opacity are copied, NULL for an empty one.
slot_layer_t *layers_add_paint_effect(slot_layer_t *target, slot_layer_t *src) {
	target = layers_effect_target(target);
	if (target == NULL || (src != NULL && slot_layer_is_mask(src) != slot_layer_is_mask(target))) {
		return NULL;
	}
	slot_layer_t *e = layers_new_effect_slot(target, src != NULL ? src->name : tr("Paint"));
	if (e == NULL) {
		return NULL;
	}
	e->effect_paint = true;
	if (src != NULL) {
		memcpy(e->blending, src->blending, sizeof(e->blending));
		memcpy(e->opacity, src->opacity, sizeof(e->opacity));
	}
	slot_layer_alloc_result(e);
	slot_layer_clear(e, 0x00000000, NULL, 1.0, layers_default_rough, 0.0); // No strokes
	if (slot_layer_is_mask(e)) {
		history_new_black_mask();
	}
	else {
		history_new_layer();
	}
	history_keep_layer_props(e);
	if (src != NULL) { // The strokes are a second step, like the pixels of Paste as Mask
		history_paste_pixels(tr("Paste"));
		gpu_texture_t *from[] = {src->texpaint, src->texpaint_nor, src->texpaint_pack};
		gpu_texture_t *to[]   = {e->texpaint, e->texpaint_nor, e->texpaint_pack};
		for (i32 i = 0; i < 3 && from[i] != NULL && to[i] != NULL; ++i) {
			draw_begin(to[i], false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(from[i], 0, 0);
			draw_set_pipeline(NULL);
			draw_end();
		}
	}
	layers_update_paint_effect(e);
	make_material_parse_mesh_material();
	g_context->layers_preview_dirty = true;
	return e;
}

// Own material for a new fill mask: white base color, so the mask starts fully visible
slot_material_t *layers_new_mask_material(char *name) {
	slot_material_t  *m      = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	ui_node_canvas_t *canvas = m->canvas;
	canvas->name             = string_copy(slot_material_unique_name(canvas, name));
	ui_node_t *output        = NULL;
	for (i32 i = canvas->nodes->length - 1; i >= 0; --i) {
		ui_node_t *n = canvas->nodes->buffer[i];
		if (string_equals(n->type, "OUTPUT_MATERIAL_PBR")) {
			output = n;
		}
		else if (string_equals(n->type, "RGB")) {
			for (i32 j = canvas->links->length - 1; j >= 0; --j) {
				ui_node_link_t *l = canvas->links->buffer[j];
				if (l->from_id == n->id || l->to_id == n->id) {
					array_remove(canvas->links, l);
				}
			}
			array_remove(canvas->nodes, n);
		}
	}
	if (output != NULL) {
		f32_array_t *base = output->inputs->buffer[0]->default_value;
		base->buffer[0]   = 1.0;
		base->buffer[1]   = 1.0;
		base->buffer[2]   = 1.0;
	}
	any_array_push(g_project->_->materials, m);
	g_context->material = m;
	tab_materials_update_material();
	history_new_material();
	return m;
}

void layers_create_image_mask(asset_t *asset) {
	slot_layer_t *l = g_context->layer;
	if (slot_layer_is_mask(l) || slot_layer_is_group(l)) {
		return;
	}

	history_new_layer();
	slot_layer_t *m = layers_new_mask(false, l, -1);
	slot_layer_clear(m, 0x00000000, project_get_image(asset), 1.0, layers_default_rough, 0.0);
	g_context->layer_preview_dirty = true;
}

void layers_create_image_layer(asset_t *asset) {
	history_new_layer();
	slot_layer_t *m = layers_new_layer(false, -1, NULL);
	slot_layer_clear(m, 0x00000000, project_get_image(asset), 1.0, layers_default_rough, 0.0);
	g_context->layer_preview_dirty = true;
}

void layers_create_color_layer_on_next_frame(void *_) {
	slot_layer_t *l = layers_new_layer(false, _layers_position, NULL);
	history_new_layer();
	l->uv_type     = UV_TYPE_UVMAP;
	l->object_mask = g_context->layer_filter;
	slot_layer_clear(l, _layers_base_color, NULL, _layers_occlusion, _layers_roughness, _layers_metallic);
}

void layers_create_color_layer(i32 base_color, f32 occlusion, f32 roughness, f32 metallic, i32 position) {
	_layers_base_color = base_color;
	_layers_occlusion  = occlusion;
	_layers_roughness  = roughness;
	_layers_metallic   = metallic;
	_layers_position   = position;

	sys_notify_on_next_frame(&layers_create_color_layer_on_next_frame, NULL);
}

void layers_duplicate_layer(slot_layer_t *l) {
	if (!slot_layer_is_group(l)) {
		slot_layer_t *new_layer = slot_layer_duplicate(l);
		context_set_layer(new_layer);
		slot_layer_t_array_t *masks = slot_layer_get_masks(l, false);
		if (masks != NULL) {
			for (i32 i = 0; i < masks->length; ++i) {
				slot_layer_t *m = masks->buffer[i];
				m               = slot_layer_duplicate(m);
				m->parent       = new_layer;
				array_remove(g_project->_->layers, m);
				array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_layer), m);
			}
		}
		slot_layer_t_array_t *filters = slot_layer_get_filters(l, false); // Effects, in the same order
		if (filters != NULL) {
			for (i32 i = 0; i < filters->length; ++i) {
				slot_layer_t *f = slot_layer_duplicate(filters->buffer[i]);
				f->parent       = new_layer;
				array_remove(g_project->_->layers, f);
				array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_layer), f);
			}
		}
		context_set_layer(new_layer);
		if (slot_layer_is_effect(new_layer)) { // The copy of an effect goes above it and reads it
			layers_update_effects_of(new_layer);
		}
	}
	else {
		slot_layer_t *new_group = layers_new_group();
		new_group->geometry_mask = slot_layer_geometry_mask_copy(&l->geometry_mask);
		array_remove(g_project->_->layers, new_group);
		array_insert(g_project->_->layers, array_index_of(g_project->_->layers, l) + 1, new_group);
		// group.show_panel = true;
		for (i32 i = 0; i < slot_layer_get_children(l)->length; ++i) {
			slot_layer_t         *c         = slot_layer_get_children(l)->buffer[i];
			slot_layer_t_array_t *masks     = slot_layer_get_masks(c, false);
			slot_layer_t         *new_layer = slot_layer_duplicate(c);
			new_layer->parent               = new_group;
			array_remove(g_project->_->layers, new_layer);
			array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_group), new_layer);
			if (masks != NULL) {
				for (i32 i = 0; i < masks->length; ++i) {
					slot_layer_t *m        = masks->buffer[i];
					slot_layer_t *new_mask = slot_layer_duplicate(m);
					new_mask->parent       = new_layer;
					array_remove(g_project->_->layers, new_mask);
					array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_layer), new_mask);
				}
			}
			slot_layer_t_array_t *filters = slot_layer_get_filters(c, false); // Effects of the layer, in the same order
			if (filters != NULL) {
				for (i32 i = 0; i < filters->length; ++i) {
					slot_layer_t *f = slot_layer_duplicate(filters->buffer[i]);
					f->parent       = new_layer;
					array_remove(g_project->_->layers, f);
					array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_layer), f);
				}
			}
		}
		slot_layer_t_array_t *group_masks = slot_layer_get_masks(l, true);
		if (group_masks != NULL) {
			for (i32 i = 0; i < group_masks->length; ++i) {
				slot_layer_t *m        = group_masks->buffer[i];
				slot_layer_t *new_mask = slot_layer_duplicate(m);
				new_mask->parent       = new_group;
				array_remove(g_project->_->layers, new_mask);
				array_insert(g_project->_->layers, array_index_of(g_project->_->layers, new_group), new_mask);
			}
		}
		context_set_layer(new_group);
	}
}

// Before masks are merged: masks shown through their effects and hidden effects are dropped, the effects keep their result
static void layers_delete_covered_masks(slot_layer_t *l) {
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	if (masks == NULL) {
		return;
	}
	slot_layer_t_array_t *drop = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < masks->length; ++i) {
		slot_layer_t *m = masks->buffer[i];
		if (slot_layer_is_covered(m) || (m->effect && !slot_layer_is_visible(m))) {
			any_array_push(drop, m);
		}
	}
	for (i32 i = 0; i < drop->length; ++i) {
		slot_layer_delete(drop->buffer[i]);
	}
	// A paint effect left on top of its mask stack becomes a mask holding its result
	masks = slot_layer_get_masks(l, true);
	for (i32 i = 0; masks != NULL && i < masks->length; ++i) {
		if (slot_layer_is_paint_effect(masks->buffer[i])) {
			layers_bake_paint_effect(masks->buffer[i]);
		}
	}
}

// The top visible filter of a layer replaces its content and the filters are removed, used before destructive merges
void layers_bake_filters(slot_layer_t *l) {
	slot_layer_t_array_t *filters = slot_layer_get_filters(l, false);
	if (filters == NULL) {
		return;
	}
	for (i32 i = 0; i < filters->length; ++i) {
		if (slot_layer_is_paint_effect(filters->buffer[i])) { // Its textures hold the strokes, the result goes in their place
			layers_bake_paint_effect(filters->buffer[i]);
		}
	}
	slot_layer_t *top = NULL;
	for (i32 i = 0; i < filters->length; ++i) {
		if (slot_layer_is_visible(filters->buffer[i]) && !slot_layer_is_covered(filters->buffer[i])) {
			top = filters->buffer[i];
		}
	}
	if (top != NULL) {
		gpu_texture_t *from[] = {top->texpaint, top->texpaint_nor, top->texpaint_pack};
		gpu_texture_t *to[]   = {l->texpaint, l->texpaint_nor, l->texpaint_pack};
		for (i32 i = 0; i < 3; ++i) {
			draw_begin(to[i], false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(from[i], 0, 0);
			draw_set_pipeline(NULL);
			draw_end();
		}
		l->fill_material = NULL;
	}
	for (i32 i = 0; i < filters->length; ++i) {
		slot_layer_delete(filters->buffer[i]);
	}
}

void layers_apply_masks(slot_layer_t *l) {
	layers_delete_covered_masks(l);
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);

	if (masks != NULL) {
		for (i32 i = 0; i < masks->length - 1; ++i) {
			layers_merge_layer(masks->buffer[i + 1], masks->buffer[i], false);
			slot_layer_delete(masks->buffer[i]);
		}
		slot_layer_apply_mask(masks->buffer[masks->length - 1]);
		g_context->layer_preview_dirty = true;
	}
}

void layers_merge_down() {
	slot_layer_t *l1 = g_context->layer;

	if (slot_layer_is_group(l1)) {
		l1 = layers_merge_group(l1);
	}
	else if (slot_layer_has_masks(l1, true)) { // It is a layer
		layers_apply_masks(l1);
		context_set_layer(l1);
	}

	slot_layer_t *l0 = g_project->_->layers->buffer[array_index_of(g_project->_->layers, l1) - 1];

	if (slot_layer_is_group(l0)) {
		l0 = layers_merge_group(l0);
	}
	else if (slot_layer_has_masks(l0, true)) { // It is a layer
		layers_apply_masks(l0);
		context_set_layer(l0);
	}

	// Geometry masks go into the opacity of both layers, the merged layer has none
	util_geometry_mask_bake(l1);
	util_geometry_mask_bake(l0);

	layers_merge_layer(l0, l1, false);
	slot_layer_delete(l1);
	context_set_layer(l0);
	g_context->layer_preview_dirty = true;
}

slot_layer_t *layers_merge_group(slot_layer_t *l) {
	return layers_merge_group_history(l, true);
}

// Merges a group into one layer, history records each merge step when set
slot_layer_t *layers_merge_group_history(slot_layer_t *l, bool history) {
	if (!slot_layer_is_group(l)) {
		return NULL;
	}

	slot_layer_t_array_t *children = slot_layer_get_children(l);
	for (i32 i = 0; i < children->length; ++i) {
		layers_bake_filters(children->buffer[i]);
	}

	if (children->length == 1 && slot_layer_has_masks(children->buffer[0], false)) {
		layers_apply_masks(children->buffer[0]);
	}

	for (i32 i = 0; i < children->length - 1; ++i) {
		context_set_layer(children->buffer[children->length - 1 - i]);
		if (history) {
			history_merge_layers();
		}
		layers_merge_down();
	}

	// Now apply the group masks
	layers_delete_covered_masks(l);
	slot_layer_t_array_t *masks = slot_layer_get_masks(l, true);
	if (masks != NULL) {
		for (i32 i = 0; i < masks->length - 1; ++i) {
			layers_merge_layer(masks->buffer[i + 1], masks->buffer[i], false);
			slot_layer_delete(masks->buffer[i]);
		}
		layers_apply_mask(children->buffer[0], masks->buffer[masks->length - 1]);
	}

	// With more children the merges above already baked the geometry mask of the group
	if (children->length == 1) {
		util_geometry_mask_bake(children->buffer[0]);
	}

	children->buffer[0]->parent = NULL;
	children->buffer[0]->name   = l->name;
	if (children->buffer[0]->fill_material != NULL) {
		slot_layer_to_paint_layer(children->buffer[0]);
	}
	slot_layer_delete(l);
	return children->buffer[0];
}

// Multi selection, g_context->layer is the active layer and always counts as selected, masks are never part of it

static bool layers_selectable(slot_layer_t *l) {
	return l != NULL && !slot_layer_is_mask(l) && array_index_of(g_project->_->layers, l) >= 0;
}

bool layers_is_selected(slot_layer_t *l) {
	return l == g_context->layer || (layers_selected != NULL && array_index_of(layers_selected, l) >= 0);
}

void layers_select_clear() {
	if (layers_selected != NULL) {
		layers_selected->length = 0;
	}
}

// toggle adds or removes l like ctrl+click, range selects from the active layer to l like shift+click
void layers_select(slot_layer_t *l, bool toggle, bool range) {
	if (layers_selected == NULL) {
		layers_selected = any_array_create_from_raw((void *[]){}, 0);
	}
	slot_layer_t *active = g_context->layer;
	if ((!toggle && !range) || !layers_selectable(l)) {
		layers_select_clear();
		context_set_layer(l);
		return;
	}
	if (toggle) {
		if (layers_is_selected(l) && l != active) {
			array_remove(layers_selected, l);
			return;
		}
		if (layers_selectable(active) && array_index_of(layers_selected, active) < 0) {
			any_array_push(layers_selected, active);
		}
		if (array_index_of(layers_selected, l) < 0) {
			any_array_push(layers_selected, l);
		}
		context_set_layer(l);
		return;
	}
	i32 a  = array_index_of(g_project->_->layers, active);
	i32 b  = array_index_of(g_project->_->layers, l);
	i32 lo = a < b ? a : b;
	i32 hi = a < b ? b : a;
	layers_select_clear();
	for (i32 i = lo < 0 ? 0 : lo; i <= hi; ++i) {
		slot_layer_t *c = g_project->_->layers->buffer[i];
		if (layers_selectable(c)) {
			any_array_push(layers_selected, c);
		}
	}
	context_set_layer(l);
}

// Selected layers and groups from the bottom up, the active layer included, valid until the next call
slot_layer_t_array_t *layers_get_selected() {
	static slot_layer_t_array_t *res = NULL;
	if (res == NULL) {
		res = any_array_create_from_raw((void *[]){}, 0);
	}
	res->length = 0;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (layers_selectable(l) && layers_is_selected(l)) {
			any_array_push(res, l);
		}
	}
	return res;
}

// Selected layers whose group is not selected too, those move and get deleted with the group
static slot_layer_t_array_t *layers_get_selected_roots() {
	slot_layer_t_array_t *sel = layers_get_selected();
	slot_layer_t_array_t *res = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < sel->length; ++i) {
		slot_layer_t *l = sel->buffer[i];
		if (l->parent == NULL || !layers_is_selected(l->parent)) {
			any_array_push(res, l);
		}
	}
	return res;
}

void layers_delete_selected() {
	slot_layer_t_array_t *sel = layers_get_selected_roots();
	for (i32 i = sel->length - 1; i >= 0; --i) {
		slot_layer_t *l = sel->buffer[i];
		if (array_index_of(g_project->_->layers, l) >= 0 && tab_layers_can_delete(l)) {
			tab_layers_delete_layer(l);
		}
	}
	layers_select_clear();
	make_material_parse_mesh_material();
}

// Moves a layer below the lowest child of the group, so the group keeps the order of the layers added to it
static void layers_set_group_bottom(slot_layer_t *l, slot_layer_t *group) {
	slot_layer_t_array_t *children = slot_layer_get_recursive_children(group);
	i32                   lowest   = array_index_of(g_project->_->layers, group);
	for (i32 i = 0; children != NULL && i < children->length; ++i) {
		i32 j = array_index_of(g_project->_->layers, children->buffer[i]);
		if (j < lowest) {
			lowest = j;
		}
	}
	i32 from = array_index_of(g_project->_->layers, l);
	i32 to   = from < lowest ? lowest - 1 : lowest;
	if (to != from && slot_layer_can_move(l, to)) {
		slot_layer_move(l, to);
	}
	if (l->parent != group) { // Already right below the group: only the parent changes
		history_order_layers_begin();
		history_order_layers(array_index_of(g_project->_->layers, l));
		l->parent = group;
		history_order_layers_end();
	}
}

// New group holding the selected layers that are not in a group yet
slot_layer_t *layers_group_selected() {
	slot_layer_t_array_t *sel    = layers_get_selected();
	slot_layer_t_array_t *layers = any_array_create_from_raw((void *[]){}, 0);
	for (i32 i = 0; i < sel->length; ++i) {
		slot_layer_t *l = sel->buffer[i];
		if (slot_layer_is_layer(l) && !slot_layer_is_filter(l) && !slot_layer_is_in_group(l)) {
			any_array_push(layers, l);
		}
	}
	if (layers->length == 0) {
		return NULL;
	}
	slot_layer_t *top = layers->buffer[layers->length - 1];
	context_set_layer(top);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *group    = layers_new_group();
	array_remove(g_project->_->layers, group);
	array_insert(g_project->_->layers, array_index_of(g_project->_->layers, top) + 1, group);
	top->parent = group;
	tab_layers_remap_all_layer_pointers(pointers);
	context_set_layer(group);
	history_new_group();
	for (i32 i = layers->length - 2; i >= 0; --i) {
		layers_set_group_bottom(layers->buffer[i], group);
	}
	layers_select_clear();
	context_set_layer(group);
	make_material_parse_mesh_material();
	return group;
}

// Non destructive flatten of the selected layers: they go into a new group, which is flattened
slot_layer_t *layers_flatten_selected() {
	slot_layer_t *group = layers_group_selected();
	if (group == NULL) {
		return NULL;
	}
	history_flatten_group();
	return layers_flatten_group(group);
}

// Moves the selected layers to one position keeping their order, in one undo step
void layers_move_selected(i32 to) {
	slot_layer_t_array_t *sel = layers_get_selected_roots();
	if (sel->length == 0) {
		return;
	}
	slot_layer_t         *active = g_context->layer;
	slot_layer_t_array_t *below  = any_array_create_from_raw((void *[]){}, 0); // Layers below the drop position go up
	slot_layer_t_array_t *above  = any_array_create_from_raw((void *[]){}, 0); // The others go down
	for (i32 k = 0; k < sel->length; ++k) {
		any_array_push(array_index_of(g_project->_->layers, sel->buffer[k]) < to ? below : above, sel->buffer[k]);
	}
	history_order_layers_begin();
	history_order_layers(to);
	history_order_batch = true;
	// Down: the highest goes first, each one lands below the previous
	for (i32 k = above->length - 1; k >= 0; --k) {
		if (slot_layer_can_move(above->buffer[k], to)) {
			slot_layer_move(above->buffer[k], to);
		}
	}
	// Up: without layers going down the lowest goes first to the drop position, each one lands on top of the previous.
	// Otherwise the highest goes first right below the layers that went down, keeping their order.
	for (i32 k = 0; k < below->length; ++k) {
		slot_layer_t *l      = above->length == 0 ? below->buffer[k] : below->buffer[below->length - 1 - k];
		slot_layer_t *anchor = above->length == 0 ? NULL : k == 0 ? above->buffer[0] : below->buffer[below->length - k];
		i32           dest   = to;
		if (anchor != NULL) { // Right below the anchor and its masks and filters
			dest = array_index_of(g_project->_->layers, anchor);
			for (i32 i = dest - 1; i >= 0 && g_project->_->layers->buffer[i]->parent == anchor; --i) {
				dest = i;
			}
			dest -= 1;
		}
		if (dest != array_index_of(g_project->_->layers, l) && slot_layer_can_move(l, dest)) {
			slot_layer_move(l, dest);
		}
	}
	history_order_batch = false;
	history_order_layers_end();
	if (layers_has_effects()) {
		layers_update_effects();
	}
	context_set_layer(active);
	make_material_parse_mesh_material();
}

// The effect src goes on top of the effect stack of target with a copy of its material, a paint effect with a copy of
// its strokes
slot_layer_t *layers_paste_effect(slot_layer_t *src, slot_layer_t *target) {
	if (src == NULL || target == NULL || !slot_layer_is_effect(src) || (src->fill_material == NULL && !src->effect_paint) ||
	    array_index_of(g_project->_->layers, src) < 0) {
		return NULL;
	}
	if (slot_layer_is_effect(target) && slot_layer_effect_base(target) != NULL) {
		target = slot_layer_effect_base(target);
	}
	if (slot_layer_is_group(target) || slot_layer_is_mask(target) != slot_layer_is_mask(src) || target->texpaint_sculpt != NULL ||
	    g_project->_->layers->length > layers_max_layers) {
		return NULL;
	}
	if (src->effect_paint) {
		return layers_add_paint_effect(target, src);
	}
	slot_material_t  *m      = slot_material_create(g_project->_->materials->buffer[0]->data, NULL);
	ui_node_canvas_t *canvas = util_clone_canvas(src->fill_material->canvas);
	canvas->name             = string_copy(slot_material_unique_name(canvas, string_tmp("%s %s", target->name, src->name)));
	m->canvas                = canvas;
	any_array_push(g_project->_->materials, m);
	g_context->material = m;
	tab_materials_update_material();
	history_new_material();
	return layers_add_effect_slot(target, m, src->name);
}

// Copy of src placed above target, the copy and the move are separate undo steps. An effect goes into the effect stack of target.
bool layers_paste(slot_layer_t *src, slot_layer_t *target) {
	if (src != NULL && slot_layer_is_effect(src)) {
		return layers_paste_effect(src, target) != NULL;
	}
	if (src == NULL || target == NULL || array_index_of(g_project->_->layers, src) < 0 || slot_layer_is_mask(src) || slot_layer_is_mask(target)) {
		return false;
	}
	context_set_layer(src);
	history_duplicate_layer();
	layers_duplicate_layer(src);
	slot_layer_t *copy = g_context->layer;
	if (target != src) {
		i32 target_index = array_index_of(g_project->_->layers, target);
		i32 to           = array_index_of(g_project->_->layers, copy) < target_index ? target_index : target_index + 1;
		if (slot_layer_can_move(copy, to)) {
			slot_layer_move(copy, to);
		}
	}
	context_set_layer(copy);
	make_material_parse_mesh_material();
	g_context->layers_preview_dirty = true;
	return true;
}

// New mask on target holding the pixels of the mask src
slot_layer_t *layers_paste_mask(slot_layer_t *src, slot_layer_t *target) {
	if (src == NULL || target == NULL || array_index_of(g_project->_->layers, src) < 0 || !slot_layer_is_mask(src) || slot_layer_is_mask(target) ||
	    slot_layer_is_filter(target)) {
		return NULL;
	}
	context_set_layer(target);
	slot_layer_t_array_t *pointers = tab_layers_init_layer_map();
	slot_layer_t         *m        = layers_new_mask(false, target, -1);
	if (m == NULL) {
		return NULL;
	}
	tab_layers_remap_all_layer_pointers(pointers);
	m->mask_opacity = src->mask_opacity;
	history_new_white_mask();
	history_keep_layer_props(m); // Redo gives back the opacity
	slot_layer_clear(m, 0xffffffff, NULL, 1.0, layers_default_rough, 0.0);
	context_set_layer(m);
	history_paste_mask();
	draw_begin(m->texpaint, false, 0);
	draw_set_pipeline(pipes_copy);
	draw_scaled_image(slot_layer_result(src, 0), 0, 0, m->texpaint->width, m->texpaint->height); // What the stack of a paint effect shows
	draw_set_pipeline(NULL);
	draw_end();
	make_material_parse_mesh_material();
	g_context->layers_preview_dirty = true;
	return m;
}

// Non destructive merge: a merged copy of the group goes right above it and the group is hidden
slot_layer_t *layers_flatten_group(slot_layer_t *group) {
	if (!slot_layer_is_group(group) || slot_layer_get_children(group) == NULL) {
		return NULL;
	}
	layers_duplicate_layer(group);
	slot_layer_t *flat     = layers_merge_group_history(g_context->layer, false);
	char         *old_name = flat->name;
	flat->name             = string_copy(slot_layer_unique_name(flat, string_tmp("%s %s", group->name, tr("Flattened"))));
	tab_stages_rename_layer(old_name, flat->name); // The layers tab lists the names of the stage
	tab_stages_add_layer(flat->name);
	group->visible = false;
	context_set_layer(flat);
	make_material_parse_mesh_material();
	g_context->layers_preview_dirty = true;
	return flat;
}

void layers_merge_layer(slot_layer_t *l0, slot_layer_t *l1, bool use_mask) {
	if (!l1->visible || slot_layer_is_group(l1) || (l1->effect && !slot_layer_is_visible(l1))) {
		return;
	}
	slot_layer_t  v1;
	slot_layer_t *t1 = slot_layer_result_view(l1, &v1); // Textures of what a paint effect shows, l1 for opacity and blending

	layers_make_temp_img();

	draw_begin(layers_temp_image, false, 0); // Copy to temp
	draw_set_pipeline(pipes_copy);
	draw_image(l0->texpaint, 0, 0);
	draw_set_pipeline(NULL);
	draw_end();

	render_target_t      *empty_rt = any_map_get(render_path_render_targets, "empty_white");
	gpu_texture_t        *empty    = empty_rt->_image;
	gpu_texture_t        *mask     = empty;
	slot_layer_t_array_t *l1masks  = use_mask ? slot_layer_get_composite_masks(l1) : NULL;
	if (l1masks != NULL) {
		// for (let i: i32 = 1; i < l1masks.length - 1; ++i) {
		// 	merge_layer(l1masks[i + 1], l1masks[i]);
		// }
		layers_make_temp_mask_img();
		_gpu_begin(pipes_temp_mask_image, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_merge_mask);
		gpu_set_texture(pipes_tex0_merge_mask, slot_layer_result(l1masks->buffer[0], 0));
		gpu_set_texture(pipes_texa_merge_mask, empty);
		gpu_set_float(pipes_opac_merge_mask, l1masks->buffer[0]->mask_opacity);
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MIX);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
		mask = pipes_temp_mask_image;
	}

	if (slot_layer_is_mask(l1)) {
		_gpu_begin(l0->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_merge_mask);
		gpu_set_texture(pipes_tex0_merge_mask, t1->texpaint);
		gpu_set_texture(pipes_texa_merge_mask, layers_temp_image);
		gpu_set_float(pipes_opac_merge_mask, slot_layer_get_opacity(l1));
		gpu_set_int(pipes_blending_merge_mask, BLEND_TYPE_MIX);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}

	if (slot_layer_is_layer(l1)) {
		if (l1->paint_base) {
			_gpu_begin(l0->texpaint, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, t1->texpaint);
			gpu_set_texture(pipes_tex1, empty);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_channel_opacity(l1, LAYER_CH_BASE));
			gpu_set_float(pipes_tex1w, empty->width);
			gpu_set_int(pipes_blending, slot_layer_get_blending(l1, LAYER_CH_BASE));
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l0->texpaint_nor != NULL) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(l0->texpaint_nor, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			if (l1->paint_nor) {
				_gpu_begin(l0->texpaint_nor, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
				gpu_set_pipeline(pipes_merge);
				gpu_set_texture(pipes_tex0, t1->texpaint);
				gpu_set_texture(pipes_tex1, t1->texpaint_nor);
				gpu_set_texture(pipes_texmask, mask);
				gpu_set_texture(pipes_texa, layers_temp_image);
				gpu_set_float(pipes_opac, slot_layer_get_channel_opacity(l1, LAYER_CH_NOR));
				gpu_set_float(pipes_tex1w, t1->texpaint_nor->width);
				gpu_set_int(pipes_blending, layers_nor_merge_mode(l1));
				gpu_set_vertex_buffer(const_data_screen_aligned_vb);
				gpu_set_index_buffer(const_data_screen_aligned_ib);
				gpu_draw();
				gpu_end();
			}
		}

		if (l0->texpaint_pack != NULL) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(l0->texpaint_pack, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			if (l1->paint_occ || l1->paint_rough || l1->paint_met || l1->paint_height) {
				layers_commands_merge_pack(l0->texpaint_pack, t1, mask);
			}
		}
	}
}

void layers_draw_mesh_materials() {
	bool any = false;
	for (i32 i = 0; i < g_project->_->paint_objects->length; ++i) {
		if (tab_meshes_get_override(g_project->_->paint_objects->buffer[i]) >= 0) {
			any = true;
			break;
		}
	}
	if (!any) {
		return;
	}

	string_array_t *additional = any_array_create_from_raw(
	    (void *[]){
	        "expb",
	        "expc",
	        "texpaint_blend0",
	    },
	    3);
	render_path_set_target("expa", additional, NULL, GPU_CLEAR_NONE, 0, 0.0);
	render_path_bind_target("main", "gbufferD");
	render_path_bind_target("texpaint_blend1", "paintmask");
	render_path_base_draw_overrides(NULL);
}

static gpu_texture_t *layers_udim_a    = NULL;
static gpu_texture_t *layers_udim_b    = NULL;
static gpu_texture_t *layers_udim_c    = NULL;
static gpu_texture_t *layers_udim_mask = NULL;

static gpu_texture_t *_layers_udim_target(gpu_texture_t **t, gpu_texture_t *like) {
	if (*t != NULL && ((*t)->width != like->width || (*t)->height != like->height || (*t)->format != like->format)) {
		gpu_delete_texture(*t);
		*t = NULL;
	}
	if (*t == NULL) {
		*t = gpu_create_render_target(like->width, like->height, like->format);
	}
	return *t;
}

static gpu_texture_t *_layers_udim_copy(gpu_texture_t **target, gpu_texture_t *src, i32 slot, bool to_tile) {
	gpu_texture_t *dst    = _layers_udim_target(target, src);
	i32            stride = util_mesh_atlas_stride();
	f32            cx     = (slot % stride) / (f32)stride;
	f32            cy     = (slot / stride) / (f32)stride;
	draw_begin(dst, true, 0x00000000);
	draw_set_pipeline(pipes_copy);
	if (to_tile) {
		draw_scaled_sub_image(src, cx * src->width, cy * src->height, src->width / (f32)stride, src->height / (f32)stride, 0, 0, dst->width, dst->height);
	}
	else {
		draw_scaled_image(src, cx * dst->width, cy * dst->height, dst->width / (f32)stride, dst->height / (f32)stride);
	}
	draw_set_pipeline(NULL);
	draw_end();
	return dst;
}

// Remap layer textures between the shared udim atlas and a single tile, mask is remapped in place when set
slot_layer_t *layers_udim_remap(slot_layer_t *l, gpu_texture_t **mask, i32 slot, bool to_tile) {
	static slot_layer_t remapped;
	remapped               = *l;
	remapped.texpaint      = _layers_udim_copy(&layers_udim_a, l->texpaint, slot, to_tile);
	remapped.texpaint_nor  = _layers_udim_copy(&layers_udim_b, l->texpaint_nor, slot, to_tile);
	remapped.texpaint_pack = _layers_udim_copy(&layers_udim_c, l->texpaint_pack, slot, to_tile);
	if (mask != NULL) {
		*mask = _layers_udim_copy(&layers_udim_mask, *mask, slot, to_tile);
	}
	return &remapped;
}

// Atlas slot of a layer assigned to a single udim tile object, -1 otherwise
i32 layers_udim_tile_slot(slot_layer_t *l) {
	i32 mask = slot_layer_get_object_mask(l);
	if (!util_mesh_udim_active() || l->uv_map == 1 || mask <= 0 || mask > g_project->_->paint_objects->length) {
		return -1;
	}
	i32 tile = util_mesh_udim_tile(g_project->_->paint_objects->buffer[mask - 1]->base->name);
	return tile < 0 ? -1 : util_mesh_udim_slot(tile);
}

static gpu_texture_t *layers_move_a    = NULL;
static gpu_texture_t *layers_move_b    = NULL;
static gpu_texture_t *layers_move_c    = NULL;
static gpu_texture_t *layers_move_mask = NULL;

static gpu_texture_t *_layers_move_copy(gpu_texture_t **target, gpu_texture_t *src, gpu_pipeline_t *pipe, i32 dx, i32 dy) {
	gpu_texture_t *dst = _layers_udim_target(target, src);
	draw_begin(dst, true, 0x00000000);
	draw_set_pipeline(pipe);
	draw_image(src, dx, dy);
	draw_set_pipeline(NULL);
	draw_end();
	return dst;
}

// Layer with the pending 2D view move applied, the layer itself when it is not being moved
static slot_layer_t *layers_move_preview(slot_layer_t *l) {
	i32 dx, dy;
	if (ui_view2d_move_layer(&dx, &dy) != l) {
		return l;
	}
	static slot_layer_t moved;
	moved = *l;
	if (slot_layer_is_mask(l)) {
		moved.texpaint = _layers_move_copy(&layers_move_mask, l->texpaint, pipes_copy8, dx, dy);
	}
	else {
		gpu_pipeline_t *pipe = base_bits == TEXTURE_BITS_BITS8 ? pipes_copy : base_bits == TEXTURE_BITS_BITS16 ? pipes_copy64 : pipes_copy128;
		moved.texpaint       = _layers_move_copy(&layers_move_a, l->texpaint, pipe, dx, dy);
		moved.texpaint_nor   = _layers_move_copy(&layers_move_b, l->texpaint_nor, pipe, dx, dy);
		moved.texpaint_pack  = _layers_move_copy(&layers_move_c, l->texpaint_pack, pipe, dx, dy);
	}
	return &moved;
}

slot_layer_t *layers_flatten(bool height_to_normal, slot_layer_t_array_t *layers) {
	if (layers == NULL) {
		layers = g_project->_->layers;
	}
	layers_make_temp_img();
	layers_make_export_img();
	render_target_t *empty_rt = any_map_get(render_path_render_targets, "empty_white");
	gpu_texture_t   *empty    = empty_rt->_image;

	// Clear export layer
	_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(0.5, 0.5, 1.0, 0.0), 0.0);
	gpu_end();
	_gpu_begin(layers_expc, NULL, NULL, GPU_CLEAR_COLOR, color_from_floats(1.0, 0.0, 0.0, 0.0), 0.0);
	gpu_end();

	// Flatten layers
	for (i32 i = 0; i < layers->length; ++i) {
		slot_layer_t *l1 = layers->buffer[i];
		if (!slot_layer_is_visible(l1)) {
			continue;
		}
		if (!slot_layer_is_layer(l1)) {
			continue;
		}
		if (slot_layer_is_covered(l1)) {
			continue;
		}

		gpu_texture_t        *mask    = empty;
		slot_layer_t_array_t *l1masks = slot_layer_get_composite_masks(l1);
		if (l1masks != NULL) {
			layers_make_temp_mask_img();
			draw_begin(pipes_temp_mask_image, GPU_CLEAR_COLOR, 0xffffffff);
			draw_end();
			slot_layer_t *l1 = ALLOC_INIT(slot_layer_t, {.texpaint = pipes_temp_mask_image});
			for (i32 i = 0; i < l1masks->length; ++i) {
				layers_merge_layer(l1, layers_move_preview(l1masks->buffer[i]), false);
			}
			mask = pipes_temp_mask_image;
		}
		mask = util_geometry_mask_apply(l1, mask);

		slot_layer_t view;
		l1 = slot_layer_result_view(l1, &view); // A paint effect on top of the stack shows its result
		l1 = layers_move_preview(l1);

		// Flattened layers are in shared atlas space
		i32 tile_slot = layers_udim_tile_slot(l1);
		if (tile_slot >= 0) {
			l1 = layers_udim_remap(l1, mask != empty ? &mask : NULL, tile_slot, false);
		}

		if (l1->paint_base) {
			draw_begin(layers_temp_image, false, 0); // Copy to temp
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expa, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			// if (g_context->tool == TOOL_TYPE_CURSOR) {
			// 	// Do not multiply basecol by alpha
			// 	draw_begin(layers_expa, false, 0); // Copy to temp
			// 	draw_set_pipeline(pipes_copy);
			// 	draw_image(l1->texpaint, 0, 0);
			// 	draw_set_pipeline(NULL);
			// 	draw_end();
			// }
			// else {
			_gpu_begin(layers_expa, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, empty);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_channel_opacity(l1, LAYER_CH_BASE));
			gpu_set_float(pipes_tex1w, empty->width);
			gpu_set_int(pipes_blending, layers->length > 1 ? slot_layer_get_blending(l1, LAYER_CH_BASE) : 0);
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
			// }
		}

		if (l1->paint_nor) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expb, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			_gpu_begin(layers_expb, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
			gpu_set_pipeline(pipes_merge);
			gpu_set_texture(pipes_tex0, l1->texpaint);
			gpu_set_texture(pipes_tex1, l1->texpaint_nor);
			gpu_set_texture(pipes_texmask, mask);
			gpu_set_texture(pipes_texa, layers_temp_image);
			gpu_set_float(pipes_opac, slot_layer_get_channel_opacity(l1, LAYER_CH_NOR));
			gpu_set_float(pipes_tex1w, l1->texpaint_nor->width);
			gpu_set_int(pipes_blending, layers_nor_merge_mode(l1));
			gpu_set_vertex_buffer(const_data_screen_aligned_vb);
			gpu_set_index_buffer(const_data_screen_aligned_ib);
			gpu_draw();
			gpu_end();
		}

		if (l1->paint_occ || l1->paint_rough || l1->paint_met || l1->paint_height) {
			draw_begin(layers_temp_image, false, 0);
			draw_set_pipeline(pipes_copy);
			draw_image(layers_expc, 0, 0);
			draw_set_pipeline(NULL);
			draw_end();

			layers_commands_merge_pack(layers_expc, l1, mask);
		}
	}

	if (util_mesh_udim_active()) {
		layers_draw_mesh_materials();
	}

	slot_layer_t *l0 = ALLOC_INIT(slot_layer_t, {.texpaint = layers_expa, .texpaint_nor = layers_expb, .texpaint_pack = layers_expc});

	// Merge height map into normal map
	if (height_to_normal && make_material_height_used) {
		draw_begin(layers_temp_image, false, 0);
		draw_set_pipeline(pipes_copy);
		draw_image(l0->texpaint_nor, 0, 0);
		draw_set_pipeline(NULL);
		draw_end();

		_gpu_begin(l0->texpaint_nor, NULL, NULL, GPU_CLEAR_NONE, 0, 0.0);
		gpu_set_pipeline(pipes_merge);
		gpu_set_texture(pipes_tex0, layers_temp_image);
		gpu_set_texture(pipes_tex1, l0->texpaint_pack);
		gpu_set_texture(pipes_texmask, empty);
		gpu_set_texture(pipes_texa, empty);
		gpu_set_float(pipes_opac, 1.0);
		gpu_set_float(pipes_tex1w, l0->texpaint_pack->width);
		gpu_set_int(pipes_blending, 104);
		gpu_set_vertex_buffer(const_data_screen_aligned_vb);
		gpu_set_index_buffer(const_data_screen_aligned_ib);
		gpu_draw();
		gpu_end();
	}

	return l0;
}

void layers_on_resized_on_next_frame(void *_) {
	layers_resize();
	slot_layer_t    *_layer    = g_context->layer;
	slot_material_t *_material = g_context->material;
	for (i32 i = 0; i < g_project->_->layers->length; ++i) {
		slot_layer_t *l = g_project->_->layers->buffer[i];
		if (l->fill_material != NULL) {
			g_context->layer    = l;
			g_context->material = l->fill_material;
			layers_set_object_mask();
			layers_update_fill_layer(true);
		}
		else if (slot_layer_is_paint_effect(l)) { // Its scaled result is redone from the refilled input
			layers_update_paint_effect(l);
		}
	}
	g_context->layer    = _layer;
	g_context->material = _material;
	layers_set_object_mask();
	make_material_parse_paint_material(true);
}

void layers_on_resized() {
	sys_notify_on_next_frame(&layers_on_resized_on_next_frame, NULL);
	util_uv_uvmap              = NULL;
	util_uv_uvmap_cached       = false;
	util_uv_trianglemap        = NULL;
	util_uv_trianglemap_cached = false;
	util_uv_dilatemap_cached   = false;
	render_path_raytrace_ready = false;
	util_geometry_mask_invalidate();
}

void tab_layers_remap_layer_pointers(ui_node_t_array_t *nodes, i32_imap_t *pointer_map) {
	for (i32 i = 0; i < nodes->length; ++i) {
		ui_node_t *n = nodes->buffer[i];
		if (string_equals(n->type, "LAYER") || string_equals(n->type, "LAYER_MASK")) {
			i32 i = n->buttons->buffer[0]->default_value->buffer[0];
			if (i32_imap_get(pointer_map, i) != -1) {
				n->buttons->buffer[0]->default_value->buffer[0] = i32_imap_get(pointer_map, i);
			}
		}
	}
}

// Material and mesh nodes, after layers were added, removed or moved
void tab_layers_remap_all_layer_pointers(slot_layer_t_array_t *old_layers) {
	for (i32 i = 0; i < g_project->_->materials->length; ++i) {
		slot_material_t *m = g_project->_->materials->buffer[i];
		tab_layers_remap_layer_pointers(m->canvas->nodes, tab_layers_fill_layer_map(old_layers));
	}
	ui_node_canvas_t_array_t *mesh_canvases = nodes_mesh_canvases();
	for (i32 i = 0; i < mesh_canvases->length; ++i) {
		tab_layers_remap_layer_pointers(mesh_canvases->buffer[i]->nodes, tab_layers_fill_layer_map(old_layers));
	}
}

slot_layer_t_array_t *tab_layers_init_layer_map() {
	return array_slice(g_project->_->layers, 0, g_project->_->layers->length);
}

i32_imap_t *tab_layers_fill_layer_map(slot_layer_t_array_t *old_layers) {
	// Old index -> new index, 9999 for removed layers
	i32_imap_t *res = any_imap_create();
	for (i32 i = 0; i < old_layers->length; ++i) {
		i32 new_index = array_index_of(g_project->_->layers, old_layers->buffer[i]);
		i32_imap_set(res, i, new_index > -1 ? new_index : 9999);
	}
	return res;
}

void tab_layers_make_mask_preview_rgba32_on_next_frame(void *_) {
	slot_layer_t *l = tab_layers_l;
	draw_begin(g_context->mask_preview_rgba32, true, 0xff000000);
	draw_set_pipeline(ui_view2d_pipe);
	gpu_set_int(ui_view2d_channel_loc, 1);
	draw_image(l->texpaint_preview, 0, 0);
	draw_end();
	draw_set_pipeline(NULL);
}

void tab_layers_make_mask_preview_rgba32(slot_layer_t *l) {
	if (g_context->mask_preview_rgba32 == NULL) {
		g_context->mask_preview_rgba32 = gpu_create_render_target(util_render_layer_preview_size, util_render_layer_preview_size, GPU_TEXTURE_FORMAT_RGBA32);
	}
	// Convert from R8 to RGBA32 for tooltip display
	if (g_context->mask_preview_last != l) {
		g_context->mask_preview_last = l;
		tab_layers_l                 = l;
		sys_notify_on_next_frame(&tab_layers_make_mask_preview_rgba32_on_next_frame, NULL);
	}
}
