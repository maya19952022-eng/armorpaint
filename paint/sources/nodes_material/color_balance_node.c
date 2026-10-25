#include "../global.h"

char *color_balance_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *col        = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *shadows    = parser_material_parse_vector_input(node->inputs->buffer[1]);
	char *midtones   = parser_material_parse_vector_input(node->inputs->buffer[2]);
	char *highlights = parser_material_parse_vector_input(node->inputs->buffer[3]);
	bool  preserve   = node->buttons->buffer[0]->default_value->buffer[0] > 0;
	char *store      = parser_material_store_var_name(node);
	char *lum        = "float3(0.2126, 0.7152, 0.0722)"; // Rec. 709
	char *half       = "float3(0.5, 0.5, 0.5)";
	parser_material_write(parser_material_kong, string_tmp("float3 %s_in = %s;", store, col));
	parser_material_write(parser_material_kong, string_tmp("float %s_l = dot(%s_in, %s);", store, store, lum));
	// Tonal range weights, they sum to 1
	parser_material_write(parser_material_kong, string_tmp("float %s_t = clamp(%s_l, 0.0, 1.0);", store, store));
	parser_material_write(parser_material_kong, string_tmp("float %s_ws = (1.0 - %s_t) * (1.0 - %s_t);", store, store, store));
	parser_material_write(parser_material_kong, string_tmp("float %s_wh = %s_t * %s_t;", store, store, store));
	parser_material_write(parser_material_kong, string_tmp("float %s_wm = 1.0 - %s_ws - %s_wh;", store, store, store));
	parser_material_write(parser_material_kong, string_tmp("float3 %s_res = %s_in + (%s - %s) * %s_ws + (%s - %s) * %s_wm + (%s - %s) * %s_wh;", store, store,
	                                                       shadows, half, store, midtones, half, store, highlights, half, store));
	parser_material_write(parser_material_kong, string_tmp("%s_res = clamp(%s_res, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));", store, store));
	if (preserve) {
		parser_material_write(parser_material_kong,
		                      string_tmp("%s_res = clamp(%s_res * (%s_l / max(dot(%s_res, %s), %s)), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));", store,
		                                 store, store, store, lum, f32_to_string(parser_material_eps)));
	}
	return string_tmp("%s_res", store);
}

void color_balance_node_init() {

	ui_node_t *color_balance_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Color Balance"),
	                           .type   = "COLOR_BALANCE", // extension
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff448c6d,
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
	                                                                 .name          = _tr("Shadows"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.5, 0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Midtones"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.5, 0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Highlights"),
	                                                                 .type          = "RGBA",
	                                                                 .color         = 0xffc7c729,
	                                                                 .default_value = f32_array_create_xyzw(0.5, 0.5, 0.5, 1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               4),
	                           .outputs = any_array_create_from_raw(
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
	                               },
	                               1),
	                           .buttons = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Preserve Luminosity"),
	                                                                 .type          = "BOOL",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(1),
	                                                                 .data          = NULL,
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_color, color_balance_node_def);
	any_map_set(parser_material_node_vectors, "COLOR_BALANCE", color_balance_node_vector);
}
