#include "../global.h"

// The input is baked to a texture by make_material_bake_node_preview, then blurred here
char *sharpen_node_vector(ui_node_t *node, ui_node_socket_t *socket) {
	if (parser_material_sharpen_passthrough == node) {
		return parser_material_parse_vector_input(node->inputs->buffer[0]);
	}
	char *col      = parser_material_parse_vector_input(node->inputs->buffer[0]);
	char *amount   = parser_material_parse_value_input(node->inputs->buffer[1], false);
	char *radius   = parser_material_parse_value_input(node->inputs->buffer[2], false);
	char *store    = parser_material_store_var_name(node);
	char *tex_name = string_tmp("texblur_%s", make_material_node_preview_id(node, NULL)); // Bound like the blur texture
	node_shader_add_texture(parser_material_kong, tex_name, string_tmp("_%s", tex_name));
	node_shader_add_constant(parser_material_kong, string_tmp("float2 %s_size", tex_name), string_tmp("_size(_%s)", tex_name));
	// Gaussian blur with sigma = radius / 2, in texels
	parser_material_write(parser_material_kong, string_tmp("float %s_r = clamp(%s, 0.0, 16.0);", store, radius));
	parser_material_write(parser_material_kong, string_tmp("float %s_n = ceil(%s_r);", store, store));
	parser_material_write(parser_material_kong, string_tmp("float %s_s = max(%s_r * %s_r * 0.5, 0.25);", store, store, store));
	parser_material_write(parser_material_kong, string_tmp("float3 %s_sum = float3(0.0, 0.0, 0.0);", store));
	parser_material_write(parser_material_kong, string_tmp("float %s_w = 0.0;", store));
	parser_material_write(parser_material_kong, string_tmp("for (int %s_i = 0; %s_i <= int(%s_n * 2.0); %s_i += 1) {", store, store, store, store));
	parser_material_write(parser_material_kong, string_tmp("for (int %s_j = 0; %s_j <= int(%s_n * 2.0); %s_j += 1) {", store, store, store, store));
	parser_material_write(parser_material_kong, string_tmp("float2 %s_o = float2(float(%s_i) - %s_n, float(%s_j) - %s_n);", store, store, store, store, store));
	parser_material_write(parser_material_kong, string_tmp("float %s_k = exp(-dot(%s_o, %s_o) / %s_s);", store, store, store, store));
	parser_material_write(parser_material_kong, string_tmp("%s_sum += sample_lod(%s, sampler_linear, tex_coord + %s_o / constants.%s_size, 0.0).rgb * %s_k;",
	                                                       store, tex_name, store, tex_name, store));
	parser_material_write(parser_material_kong, string_tmp("%s_w += %s_k;", store, store));
	parser_material_write(parser_material_kong, "}");
	parser_material_write(parser_material_kong, "}");
	// Detail from the baked texture only, a flat input stays flat
	parser_material_write(parser_material_kong, string_tmp("float3 %s_c = sample_lod(%s, sampler_linear, tex_coord, 0.0).rgb;", store, tex_name));
	return string_tmp("clamp(%s + (%s_c - %s_sum / %s_w) * %s, float3(0.0, 0.0, 0.0), float3(1.0, 1.0, 1.0))", col, store, store, store, amount);
}

void sharpen_node_init() {

	ui_node_t *sharpen_node_def =
	    ALLOC_INIT(ui_node_t, {.id     = 0,
	                           .name   = _tr("Sharpen"),
	                           .type   = "SHARPEN", // extension
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
	                                                                 .name          = _tr("Amount"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 5.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                                   ALLOC_INIT(ui_node_socket_t, {.id            = 0,
	                                                                 .node_id       = 0,
	                                                                 .name          = _tr("Radius"),
	                                                                 .type          = "VALUE",
	                                                                 .color         = 0xffa1a1a1,
	                                                                 .default_value = f32_array_create_x(1.0),
	                                                                 .min           = 0.0,
	                                                                 .max           = 16.0,
	                                                                 .precision     = 100,
	                                                                 .display       = 0}),
	                               },
	                               3),
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
	                           .buttons = any_array_create_from_raw((void *[]){}, 0),
	                           .width   = 0,
	                           .flags   = 0});

	any_array_push(nodes_material_color, sharpen_node_def);
	any_map_set(parser_material_node_vectors, "SHARPEN", sharpen_node_vector);
}
