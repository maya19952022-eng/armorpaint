#include "../global.h"

// Reads a mesh map of the project, baked by util_bake.c

static i32 mesh_map_node_slot(ui_node_t *node) {
	i32 slot = (i32)node->buttons->buffer[0]->default_value->buffer[0];
	return slot >= 0 && slot < MESH_MAP_COUNT ? slot : MESH_MAP_OCCLUSION;
}

// Mesh maps are baked in the first uv map. When painting, tex_coord carries the layer scale or a triplanar
// or decal projection, so the map is read with the mesh uvs instead. Node previews show the whole map.
// Sculpt layers paint in vertex space without mesh uvs and keep tex_coord.
static char *mesh_map_node_uv() {
	char *context = parser_material_kong->context->data->name;
	if ((!string_equals(context, "paint") && !string_equals(context, "atlas")) || g_context->layer->texpaint_sculpt != NULL) {
		return "tex_coord";
	}
	if (string_array_index_of(parser_material_kong->outs, "float2 tex_coord_mesh") == -1) {
		node_shader_context_add_elem(parser_material_kong->context, "tex", "short2norm");
		node_shader_add_out(parser_material_kong, "float2 tex_coord_mesh");
		node_shader_write_vert(parser_material_kong, "output.tex_coord_mesh = input.tex;");
	}
	return "input.tex_coord_mesh";
}

static char *mesh_map_node_sample(ui_node_t *node) {
	i32   slot = mesh_map_node_slot(node);
	char *name = string_tmp("texmeshmap%d", slot);
	node_shader_add_texture(parser_material_kong, name, string_tmp("_texmeshmap%d", slot));
	return string_tmp("sample(%s, sampler_linear, %s)", name, mesh_map_node_uv());
}

static char *mesh_map_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	return string_tmp("%s.rgb", mesh_map_node_sample(node));
}

static char *mesh_map_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	return string_tmp("%s.r", mesh_map_node_sample(node));
}

static void mesh_map_node_button(i32 node_id) {
	ui_node_t   *node   = ui_get_node(ui_nodes_get_canvas(true)->nodes, node_id);
	f32_array_t *values = node->buttons->buffer[0]->default_value;
	i32          slot   = mesh_map_node_slot(node);
	ui_push_id((ui_id_t)ui_nodes_get_nodes());
	ui_push_id((ui_id_t)node_id);

	ui_set_next_id((ui_id_t)&values->buffer[0]);
	i32 prev = slot;
	ui_combo(&slot, util_bake_slot_names(), tr("Map"), false, UI_ALIGN_LEFT, true);
	values->buffer[0] = slot;
	if (slot != prev) {
		ui_nodes_hwnd->redraws = 2;
		make_material_parse_paint_material(true); // Sample the other slot
	}

	i32 state = util_bake_state(slot, NULL);
	if (state == BAKE_STATE_QUEUED) { // Waits for the bakes ahead of it, a click takes it out of the queue
		if (ui_icon_button(tr("Queued"), ICON_STOP, UI_ALIGN_CENTER)) {
			util_bake_cancel_job(slot, NULL);
		}
		ui_nodes_hwnd->redraws = 2;
	}
	else if (state == BAKE_STATE_RUNNING) {
		f32 progress        = util_bake_progress();
		i32 _BUTTON_COL     = g_theme->BUTTON_COL;
		g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;
		char *label         = string_tmp("%d%%", (i32)(progress * 100));
		ui_set_next_id((ui_id_t)&values->buffer[0] + 1);
		ui_slider(&progress, label, 0.0, 1.0, true, 100, false, UI_ALIGN_CENTER, true);
		g_theme->BUTTON_COL = _BUTTON_COL;
		if (g_ui->is_hovered && g_ui->input_released) {
			util_bake_cancel_job(slot, NULL);
		}
		ui_nodes_hwnd->redraws = 2;
	}
	else if (ui_icon_button(util_bake_mesh_map(slot) == NULL ? tr("Bake") : tr("Rebake"), ICON_PLAY, UI_ALIGN_CENTER)) {
		util_bake_run(slot, NULL, NULL);
		ui_nodes_hwnd->redraws = 2;
	}

	ui_pop_id();
	ui_pop_id();
}

void mesh_map_node_init() {
	ui_node_t *mesh_map_node_def =
	    ALLOC_INIT(ui_node_t, {.id      = 0,
	                           .name    = _tr("Mesh Map"),
	                           .type    = "MESH_MAP", // extension
	                           .x       = 0,
	                           .y       = 0,
	                           .color   = 0xff4982a0,
	                           .inputs  = any_array_create_from_raw((void *[]){}, 0),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.0, 0.0, 0.0, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Value"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               2),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = "mesh_map_node_button",
	                                                                 .type          = "CUSTOM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(MESH_MAP_OCCLUSION),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 2}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_input, mesh_map_node_def);
	any_map_set(parser_material_node_vectors, "MESH_MAP", mesh_map_node_vector);
	any_map_set(parser_material_node_values, "MESH_MAP", mesh_map_node_value);
	any_map_set(ui_nodes_custom_buttons, "mesh_map_node_button", mesh_map_node_button);
}
