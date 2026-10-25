#include "../global.h"

char *color_selection_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *input_color = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *tolerance   = parser_material_parse_value_input(node->inputs->buffer[5], false);
	char *smooth      = parser_material_parse_value_input(node->inputs->buffer[6], false);
	i32   count       = (i32)math_floor(node->buttons->buffer[0]->default_value->buffer[0] + 0.5);
	bool  invert      = node->buttons->buffer[1]->default_value->buffer[0] > 0;
	char *store       = parser_material_store_var_name(node);
	count             = count < 1 ? 1 : count > 4 ? 4 : count;
	parser_material_write(parser_material_kong, string_tmp("float3 %s_in = %s;", store, input_color));
	parser_material_write(parser_material_kong, string_tmp("float %s_m = 0.0;", store));
	for (i32 i = 0; i < count; ++i) {
		// Same falloff as the color mask node
		char *color = parser_material_parse_vector_input(node->inputs->buffer[1 + i]);
		parser_material_write(parser_material_kong, string_tmp("%s_m = max(%s_m, clamp(1.0 - (distance(%s_in, %s) - %s) / max(%s, %s), 0.0, 1.0));", store,
		                                                       store, store, color, tolerance, smooth, f32_to_string(parser_material_eps)));
	}
	return invert ? string_tmp("(1.0 - %s_m)", store) : string_tmp("%s_m", store);
}

void color_selection_node_init() {

	ui_node_t *color_selection_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Color Selection"),
	                           .type   = "COLOR_SELECTION", // extension
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff62676d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.8, 0.8, 0.8, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color 1"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(1.0, 0.0, 0.0, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color 2"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.0, 1.0, 0.0, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color 3"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.0, 0.0, 1.0, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Color 4"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(1.0, 1.0, 0.0, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Tolerance"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.1),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.74,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Smoothness"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               7),
	                           .outputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Mask"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Count"),
	                                                                 .type          = "VALUE",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(1),
	                                                                 .data          = NULL,
	                                                                 .min           = 1.0,
	                                                                 .max           = 4.0,
	                                                                 .precision     = 1,
	                                                                 .height        = 0}),
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Invert"),
	                                                                 .type          = "BOOL",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               2),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_color, color_selection_node_def);
	any_map_set(parser_material_node_values, "COLOR_SELECTION", color_selection_node_value);
}
