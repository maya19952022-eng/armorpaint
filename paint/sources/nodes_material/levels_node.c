#include "../global.h"

char *levels_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	char *col       = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *in_black  = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *in_white  = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *gamma     = parser_material_parse_value_input(node->inputs->buffer[3], false);
	char *out_black = parser_material_parse_value_input(node->inputs->buffer[4], false);
	char *out_white = parser_material_parse_value_input(node->inputs->buffer[5], false);
	i32   channel   = (i32)node->buttons->buffer[0]->default_value->buffer[0]; // RGB, R, G, B
	char *store     = parser_material_store_var_name(node);
	char *eps       = f32_to_string(parser_material_eps);
	parser_material_write(parser_material_kong, string_tmp("float3 %s_in = %s;", store, col));
	parser_material_write(parser_material_kong,
	                      string_tmp("float3 %s_v = clamp((%s_in - %s) / max(%s - %s, %s), float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0));", store, store,
	                                 parser_material_to_vec3(in_black), in_white, in_black, eps));
	parser_material_write(parser_material_kong, string_tmp("float %s_g = 1.0 / max(%s, %s);", store, gamma, eps));
	parser_material_write(parser_material_kong, string_tmp("%s_v = pow(%s_v, float3(%s_g, %s_g, %s_g));", store, store, store, store, store));
	parser_material_write(parser_material_kong,
	                      string_tmp("%s_v = %s + (%s - %s) * %s_v;", store, parser_material_to_vec3(out_black), out_white, out_black, store));
	if (channel == 1) {
		return string_tmp("float3(%s_v.x, %s_in.y, %s_in.z)", store, store, store);
	}
	else if (channel == 2) {
		return string_tmp("float3(%s_in.x, %s_v.y, %s_in.z)", store, store, store);
	}
	else if (channel == 3) {
		return string_tmp("float3(%s_in.x, %s_in.y, %s_v.z)", store, store, store);
	}
	return string_tmp("%s_v", store);
}

void levels_node_init() {

	char      *levels_channel_data = string_tmp("%s\n%s\n%s\n%s", _tr("RGB"), _tr("R"), _tr("G"), _tr("B"));
	ui_node_t *levels_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Levels"),
	                           .type   = "LEVELS", // extension
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
	                                                                 .name          = _tr("In Black"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("In White"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Gamma"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.01,
	                                                                 .max           = 10.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Out Black"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Out White"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               6),
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
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Channel"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(levels_channel_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_color, levels_node_def);
	any_map_set(parser_material_node_vectors, "LEVELS", levels_node_vector);
}
