#include "../global.h"

char *compare_mask_node_value(ui_node_t *node, ui_node_socket_t *socket) {
	char *a         = parser_material_parse_value_input(node->inputs->buffer[0], false);
	char *b         = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *threshold = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *smooth    = parser_material_parse_value_input(node->inputs->buffer[3], false);
	i32   op        = (i32)node->buttons->buffer[0]->default_value->buffer[0]; // Greater, Less, Equal, Not Equal
	// The mask starts at the threshold and reaches 1 after the smoothness
	char *edge = string_tmp("%s, %s + max(%s, %s)", threshold, threshold, smooth, f32_to_string(parser_material_eps));
	if (op == 1) {
		return string_tmp("smoothstep(%s, (%s) - (%s))", edge, b, a);
	}
	else if (op == 2) {
		return string_tmp("(1.0 - smoothstep(%s, abs((%s) - (%s))))", edge, a, b);
	}
	else if (op == 3) {
		return string_tmp("smoothstep(%s, abs((%s) - (%s)))", edge, a, b);
	}
	return string_tmp("smoothstep(%s, (%s) - (%s))", edge, a, b);
}

void compare_mask_node_init() {

	char      *compare_mask_operation_data = string_tmp("%s\n%s\n%s\n%s", _tr("Greater"), _tr("Less"), _tr("Equal"), _tr("Not Equal"));
	ui_node_t *compare_mask_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Compare Mask"),
	                           .type   = "COMPARE_MASK", // extension
	                           .x      = 0,
	                           .y      = 0,
	                           .color  = 0xff62676d,
	                           .inputs = any_array_create_from_raw(
	                               (void *[]){
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("A"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("B"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.5),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Threshold"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(0.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
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
	                               4),
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
	                                   ALLOC_INIT(ui_node_button_t, {.name          = _tr("Operation"),
	                                                                 .type          = "ENUM",
	                                                                 .output        = -1,
	                                                                 .default_value = f32_array_create_x(0),
	                                                                 .data          = u8_array_create_from_string(compare_mask_operation_data),
	                                                                 .min           = 0.0,
	                                                                 .max           = 1.0,
	                                                                 .precision     = 100,
	                                                                 .height        = 0}),
	                               },
	                               1),
	                           .width = 0,
	                           .flags = 0});

	any_array_push(nodes_material_utilities, compare_mask_node_def);
	any_map_set(parser_material_node_values, "COMPARE_MASK", compare_mask_node_value);
}
