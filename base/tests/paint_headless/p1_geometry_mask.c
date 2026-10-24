#include "global.h"

// LAY-11: geometry mask. Two quads, geo_a in the left half of the uv space and geo_b in the right
// half, replace the default mesh. A red layer over the gray base is restricted to one of them with
// include and exclude, by itself and through its group, and the exported base color is read inside
// and outside of the mask. Also: undo and redo, save and reopen, merge down keeps the result, a fill
// stroke outside of the mask changes nothing, UDIM tiles, and a version 22 project saved before the
// geometry mask opens unchanged.
// Run with: base/tests/paint_headless/run.sh p1_geometry_mask.c

char *root      = "../../../base/tests/paint_headless";
char *dir_out   = "../smoke";
char *file_arm  = NULL;
char *dir_exp   = NULL;
float tolerance = 3.0;

int step         = 0;
int wait         = 0;
int checks       = 0;
int failures     = 0;
int export_count = 0;

object_t *cube      = NULL;
char     *cube_name = NULL; // The object is freed once removed

// Quads facing the default camera, uvs inside [0.05, 0.95] of their half
char *mesh_objects = "o geo_a\n"
                     "v -1.0 0.0 -0.5\nv -0.1 0.0 -0.5\nv -0.1 0.0 0.5\nv -1.0 0.0 0.5\n"
                     "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.95\nvt 0.05 0.95\n"
                     "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                     "o geo_b\n"
                     "v 0.1 0.0 -0.5\nv 1.0 0.0 -0.5\nv 1.0 0.0 0.5\nv 0.1 0.0 0.5\n"
                     "vt 0.55 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.55 0.95\n"
                     "f 5/5 6/6 7/7\nf 5/5 7/7 8/8\n";

// Two UDIM tile objects, each with uvs over its whole tile
char *mesh_tiles = "o tile.1001\n"
                   "v -1.0 0.0 -0.5\nv -0.1 0.0 -0.5\nv -0.1 0.0 0.5\nv -1.0 0.0 0.5\n"
                   "vt 0.05 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.05 0.95\n"
                   "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                   "o tile.1002\n"
                   "v 0.1 0.0 -0.5\nv 1.0 0.0 -0.5\nv 1.0 0.0 0.5\nv 0.1 0.0 0.5\n"
                   "vt 0.05 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.05 0.95\n"
                   "f 5/5 6/6 7/7\nf 5/5 7/7 8/8\n";

void check(int ok, char *name) {
	checks += 1;
	if (ok) {
		printf("[smoke]   ok   %s", name);
	}
	else {
		failures += 1;
		printf("[smoke]   FAIL %s", name);
	}
}

float absf(float f) {
	return f < 0.0 ? -f : f;
}

slot_layer_t *layer_by_name(char *name) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (string_equals(l->name, name)) {
			return l;
		}
	}
	return NULL;
}

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

// Red and green of the exported base color at two uv points, a at (ua, va) and b at (ub, vb)
float ar     = -1.0;
float ag     = -1.0;
float br     = -1.0;
float bg     = -1.0;
float mean_r = -1.0;
float mean_g = -1.0;
float mean_b = -1.0;

void export_base(float ua, float va, float ub, float vb) {
	ar     = -1.0;
	ag     = -1.0;
	br     = -1.0;
	bg     = -1.0;
	mean_r = -1.0;
	export_count += 1;
	char *dir = string("%s/p1_geometry%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	any_array_t *files = file_read_directory(dir);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, "_base.png")) {
			continue;
		}
		script_import_asset(string("%s/%s", dir, name), false);
		gpu_texture_t *tex    = script_get_texture(name);
		buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
		if (pixels != NULL) {
			int w  = tex->width;
			int h  = tex->height;
			int pa = ((int)(va * h) * w + (int)(ua * w)) * 4;
			int pb = ((int)(vb * h) * w + (int)(ub * w)) * 4;
			ar     = buffer_get_u8(pixels, pa);
			ag     = buffer_get_u8(pixels, pa + 1);
			br     = buffer_get_u8(pixels, pb);
			bg     = buffer_get_u8(pixels, pb + 1);
			// Mean over the whole texture
			int   n      = pixels->length / 4;
			int   stride = n / 16384 + 1;
			float sum[3] = {0.0, 0.0, 0.0};
			int   count  = 0;
			for (int p = 0; p < n; p += stride) {
				sum[0] += buffer_get_u8(pixels, p * 4);
				sum[1] += buffer_get_u8(pixels, p * 4 + 1);
				sum[2] += buffer_get_u8(pixels, p * 4 + 2);
				count += 1;
			}
			mean_r = sum[0] / count;
			mean_g = sum[1] / count;
			mean_b = sum[2] / count;
		}
		script_texture_delete(name);
	}
}

int is_red(float r, float g) {
	return r > 240.0 && g < 15.0;
}

int is_gray(float r, float g) {
	return absf(r - 128.0) <= tolerance && absf(g - 128.0) <= tolerance;
}

// Exported base color in the middle of geo_a and of geo_b
void check_halves(char *when, int red_a, int red_b) {
	export_base(0.25, 0.5, 0.75, 0.5);
	int ok = (red_a ? is_red(ar, ag) : is_gray(ar, ag)) && (red_b ? is_red(br, bg) : is_gray(br, bg));
	check(ok, string("%s: geo_a %s (%.0f %.0f), geo_b %s (%.0f %.0f)", when, red_a ? "red" : "gray", ar, ag, red_b ? "red" : "gray", br, bg));
}

// Alpha of a layer texture at a uv point, render targets can be BGRA but alpha stays last
float alpha_at(slot_layer_t *l, float u, float v) {
	buffer_t *pixels = script_get_texture_pixels(l->texpaint);
	if (pixels == NULL) {
		return -1.0;
	}
	int w = l->texpaint->width;
	int h = l->texpaint->height;
	return buffer_get_u8(pixels, ((int)(v * h) * w + (int)(u * w)) * 4 + 3);
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	cube      = script_get_context()->paint_object->base;
	cube_name = string_copy(cube->name);
	script_append_mesh_obj(mesh_objects);
	check(script_get_object("geo_a") != NULL && script_get_object("geo_b") != NULL, "two quads appended");
}

void step_remove_cube() {
	script_object_remove(cube);
}

void step_layers() {
	check(script_get_object(cube_name) == NULL, "default mesh removed");
	slot_layer_t *red = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	script_layer_set_name(red, "geo_red");
	check(script_layer_get_geometry_mode(red) == GEOMETRY_MASK_INCLUDE && !script_layer_get_geometry_object(red, script_get_object("geo_a")),
	      "a new layer has no geometry mask");
	check_halves("no mask", 1, 1);
}

void step_include() {
	slot_layer_t *red = layer_by_name("geo_red");
	object_t     *a   = script_get_object("geo_a");
	object_t     *b   = script_get_object("geo_b");
	script_layer_set_geometry_object(red, a, true);
	check(script_layer_get_geometry_object(red, a) && !script_layer_get_geometry_object(red, b), "geo_a selected");
	check(script_layer_geometry_applies(red, a) && !script_layer_geometry_applies(red, b), "include applies on geo_a only");
	check_halves("include geo_a", 1, 0);
}

void step_undo() {
	slot_layer_t *red = layer_by_name("geo_red");
	object_t     *a   = script_get_object("geo_a");
	script_undo();
	check(!script_layer_get_geometry_object(red, a), "undo removes geo_a from the mask");
	check_halves("undo", 1, 1);
	script_redo();
	check(script_layer_get_geometry_object(red, a), "redo selects geo_a again");
	check_halves("redo", 1, 0);
}

void step_exclude() {
	slot_layer_t *red = layer_by_name("geo_red");
	script_layer_set_geometry_mode(red, GEOMETRY_MASK_EXCLUDE);
	check(script_layer_get_geometry_mode(red) == GEOMETRY_MASK_EXCLUDE, "exclude mode");
	check_halves("exclude geo_a", 0, 1);
	script_undo();
	check(script_layer_get_geometry_mode(red) == GEOMETRY_MASK_INCLUDE, "undo goes back to include");
	script_redo();
	check(script_layer_get_geometry_mode(red) == GEOMETRY_MASK_EXCLUDE, "redo exclude");
}

// Red and gray pixels on the middle row of a screenshot of the viewport, geo_a is left of geo_b for the
// default camera. Returns 1 once counted, 0 while waiting for the file.
int sub        = 0; // Frame counter of a step that repeats
int step_again = 0; // Set by a step to run again on the next frame
int red_count  = 0;
int red_min    = 0; // Leftmost and rightmost red pixel
int red_max    = 0;
int gray_count = 0;
int gray_min   = 0;
int gray_max   = 0;

int viewport_row(char *name) {
	char *path = string("%s/%s.png", dir_out, name);
	if (sub == 0) {
		if (iron_file_exists(path)) {
			iron_delete_file(path);
		}
		script_screenshot(path);
	}
	if (!iron_file_exists(path) && sub < 60) {
		return 0;
	}
	red_count  = 0;
	gray_count = 0;
	script_import_asset(path, false);
	gpu_texture_t *tex    = script_get_texture(string("%s.png", name));
	buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
	if (pixels != NULL) {
		red_min  = tex->width;
		gray_min = tex->width;
		red_max  = -1;
		gray_max = -1;
		int y    = tex->height / 2;
		for (int x = 0; x < tex->width; ++x) {
			int p = (y * tex->width + x) * 4;
			int r = buffer_get_u8(pixels, p);
			int g = buffer_get_u8(pixels, p + 1);
			int b = buffer_get_u8(pixels, p + 2);
			if (r > 150 && g < 60 && b < 60) {
				red_count += 1;
				red_min = x < red_min ? x : red_min;
				red_max = x > red_max ? x : red_max;
			}
			else if (r > 100 && absf(r - g) < 20 && absf(r - b) < 20) { // Gray base color of the quads
				gray_count += 1;
				gray_min = x < gray_min ? x : gray_min;
				gray_max = x > gray_max ? x : gray_max;
			}
		}
	}
	script_texture_delete(string("%s.png", name));
	printf("[smoke]        viewport %s: red %d (x %d..%d), gray %d (x %d..%d)", name, red_count, red_min, red_max, gray_count, gray_min, gray_max);
	return 1;
}

void step_viewport_include() {
	if (sub == 0) {
		context_set_viewport_mode(1); // VIEWPORT_MODE_BASE_COLOR
		script_layer_set_geometry_mode(layer_by_name("geo_red"), GEOMETRY_MASK_INCLUDE);
	}
	if (!viewport_row("p1_geometry_include")) {
		step_again = 1;
		return;
	}
	check(red_count > 10 && gray_count > 10 && red_max < gray_min, "viewport: include geo_a shows red on the left quad only");
}

void step_viewport_exclude() {
	if (sub == 0) {
		script_layer_set_geometry_mode(layer_by_name("geo_red"), GEOMETRY_MASK_EXCLUDE);
	}
	if (!viewport_row("p1_geometry_exclude")) {
		step_again = 1;
		return;
	}
	check(red_count > 10 && gray_count > 10 && gray_max < red_min, "viewport: exclude geo_a shows red on the right quad only");
	context_set_viewport_mode(0); // VIEWPORT_MODE_LIT
}

void step_tiles_filter() {
	// Objects without a tile suffix are tile 1001: selecting 1002 leaves no geometry
	slot_layer_t *red = layer_by_name("geo_red");
	script_layer_set_geometry_tile(red, 1002, true);
	check(script_layer_get_geometry_tile(red, 1002), "tile 1002 selected");
	check(script_layer_geometry_applies(red, script_get_object("geo_a")), "exclude geo_a in tile 1002 selects nothing, the layer stays everywhere");
	check_halves("exclude geo_a in tile 1002", 1, 1);
	script_undo();
	check(!script_layer_get_geometry_tile(red, 1002), "undo removes the tile");
}

void step_save() {
	if (iron_file_exists(file_arm)) {
		iron_delete_file(file_arm);
	}
	project_filepath_set(file_arm);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	check(iron_file_exists(file_arm), "project saved");
	script_project_new();
	script_project_open(file_arm);
	slot_layer_t *red = layer_by_name("geo_red");
	object_t     *a   = script_get_object("geo_a");
	check(red != NULL && a != NULL && script_layer_get_geometry_object(red, a) && script_layer_get_geometry_mode(red) == GEOMETRY_MASK_EXCLUDE &&
	          !script_layer_get_geometry_tile(red, 1002),
	      "the geometry mask is saved in the project");
	slot_layer_t *base = script_layer_at(0);
	check(base != NULL && !script_layer_get_geometry_object(base, a), "the base layer has none");
	check_halves("reopened", 0, 1);
}

void step_group() {
	slot_layer_t *red = layer_by_name("geo_red");
	script_layer_clear_geometry_mask(red);
	check(!script_layer_get_geometry_object(red, script_get_object("geo_a")) && script_layer_get_geometry_mode(red) == GEOMETRY_MASK_INCLUDE,
	      "clear geometry mask");
	slot_layer_t *group = script_layer_new_group(red);
	check(group != NULL, "group");
	script_layer_set_name(group, "geo_group");
	script_layer_set_geometry_object(group, script_get_object("geo_b"), true);
	check(script_layer_geometry_applies(red, script_get_object("geo_a")) == 0, "the group mask applies to its layers");
	check_halves("group includes geo_b", 0, 1);
	script_layer_set_geometry_object(red, script_get_object("geo_b"), true);
	script_layer_set_geometry_mode(red, GEOMETRY_MASK_EXCLUDE);
	check_halves("group includes geo_b, layer excludes it", 0, 0);
	script_undo();
	script_undo();
	check_halves("undo the layer mask", 0, 1);
}

void step_flatten() {
	slot_layer_t *group = layer_by_name("geo_group");
	slot_layer_t *flat  = script_layer_flatten_group(group);
	check(flat != NULL && !script_layer_get_geometry_object(flat, script_get_object("geo_b")), "flatten bakes the geometry mask");
	check_halves("flattened", 0, 1);
	script_undo();
	check_halves("undo flatten", 0, 1);
}

void step_merge() {
	slot_layer_t *group = layer_by_name("geo_group");
	script_layer_clear_geometry_mask(group);
	slot_layer_t *red = layer_by_name("geo_red");
	script_layer_set_group(red, NULL);
	script_layer_set_geometry_object(red, script_get_object("geo_a"), true);
	check(red->parent == NULL, "layer out of the group");
	check_halves("before merge down", 1, 0);
	slot_layer_t *merged = script_layer_merge_down(red);
	check(merged != NULL && layer_by_name("geo_red") == NULL && !script_layer_get_geometry_object(merged, script_get_object("geo_a")),
	      "merge down leaves no geometry mask");
	check_halves("merged", 1, 0);
	script_undo();
	red = layer_by_name("geo_red");
	check(red != NULL && script_layer_get_geometry_object(red, script_get_object("geo_a")), "undo merge brings the mask back");
	check_halves("undo merge", 1, 0);
}

void step_paint() {
	slot_layer_t *paint = script_layer_new();
	script_layer_set_name(paint, "geo_paint");
	script_layer_set_geometry_object(paint, script_get_object("geo_b"), true);
	check(alpha_at(paint, 0.25, 0.5) < 1.0 && alpha_at(paint, 0.75, 0.5) < 1.0, "the paint layer starts empty");
	script_layer_set(paint);
	context_select_tool(TOOL_TYPE_FILL);
	script_paint(0.5, 0.5);
	script_paint_end();
	float a = alpha_at(paint, 0.25, 0.5);
	float b = alpha_at(paint, 0.75, 0.5);
	check(a < 1.0, string("filling outside of the mask changes nothing (alpha %.0f on geo_a)", a));
	check(b > 250.0, string("filling inside of the mask paints (alpha %.0f on geo_b)", b));
	script_undo();
	check(alpha_at(paint, 0.75, 0.5) < 1.0, "undo of the fill");
	script_layer_clear_geometry_mask(paint);
	script_paint(0.5, 0.5);
	script_paint_end();
	check(alpha_at(paint, 0.25, 0.5) > 250.0 && alpha_at(paint, 0.75, 0.5) > 250.0, "without the mask the fill paints both");
	context_select_tool(TOOL_TYPE_BRUSH);
}

void step_tiles_build() {
	script_project_new();
	cube      = script_get_context()->paint_object->base;
	cube_name = string_copy(cube->name);
	script_append_mesh_obj(mesh_tiles);
	check(script_get_object("tile.1001") != NULL && script_get_object("tile.1002") != NULL, "two udim tiles appended");
}

void step_tiles() {
	check(script_get_object(cube_name) == NULL, "default mesh removed");
	slot_layer_t *red = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	script_layer_set_name(red, "tile_red");
	// The shared layer is an atlas, tile 1001 in its top left quarter and 1002 in the top right one
	export_base(0.25, 0.25, 0.75, 0.25);
	check(is_red(ar, ag) && is_red(br, bg), string("both tiles red without a mask (%.0f %.0f, %.0f %.0f)", ar, ag, br, bg));
	script_layer_set_geometry_tile(red, 1002, true);
	check(script_layer_geometry_applies(red, script_get_object("tile.1002")) && !script_layer_geometry_applies(red, script_get_object("tile.1001")),
	      "tile 1002 selects its object");
	export_base(0.25, 0.25, 0.75, 0.25);
	check(is_gray(ar, ag) && is_red(br, bg), string("include tile 1002: 1001 gray (%.0f %.0f), 1002 red (%.0f %.0f)", ar, ag, br, bg));
	script_layer_set_geometry_mode(red, GEOMETRY_MASK_EXCLUDE);
	export_base(0.25, 0.25, 0.75, 0.25);
	check(is_red(ar, ag) && is_gray(br, bg), string("exclude tile 1002: 1001 red (%.0f %.0f), 1002 gray (%.0f %.0f)", ar, ag, br, bg));
}

// Saved by the build before the geometry mask, the layer maps end three fields earlier
void step_v22_fixture() {
	char     *file = string("%s/fixtures/p1_v22_project.arm", root);
	buffer_t *json = script_load_blob(string("%s/fixtures/p1_v22_project.json", root));
	check(json != NULL, "v22 fixture expected values readable");
	if (json == NULL) {
		return;
	}
	any_map_t *expected = json_parse_to_map(sys_buffer_to_string(json));
	script_project_new();
	script_project_open(file);
	check(script_layer_count() == parse_int(any_map_get(expected, "layer_count")), string("v22 fixture opens with %d layers", script_layer_count()));
	char *names[6] = {"Layer 1", "v22_red", "v22_mask", "v22_green", "v22_blue", "v22_group"};
	for (int i = 0; i < 6; ++i) {
		char         *key    = string("layers.%s", names[i]);
		slot_layer_t *l      = layer_by_name(names[i]);
		char         *parent = l != NULL && l->parent != NULL ? l->parent->name : "";
		int           ok     = l != NULL && script_layer_index(l) == parse_int(any_map_get(expected, string("%s.index", key))) &&
		         l->locked == parse_int(any_map_get(expected, string("%s.locked", key))) &&
		         absf(l->mask_opacity - parse_float(any_map_get(expected, string("%s.opacity", key)))) < 0.001 &&
		         string_equals(parent, any_map_get(expected, string("%s.parent", key)));
		check(ok, string("v22 fixture: layer %s keeps its properties", names[i]));
		check(l != NULL && script_layer_get_geometry_mode(l) == GEOMETRY_MASK_INCLUDE && !script_layer_get_geometry_tile(l, 1001) &&
		          script_layer_geometry_applies(l, script_get_context()->paint_object->base),
		      string("v22 fixture: layer %s has no geometry mask", names[i]));
	}
	export_base(0.5, 0.5, 0.5, 0.5);
	float r  = parse_float(any_map_get(expected, "base.r"));
	float g  = parse_float(any_map_get(expected, "base.g"));
	float b  = parse_float(any_map_get(expected, "base.b"));
	int   ok = absf(mean_r - r) <= tolerance && absf(mean_g - g) <= tolerance && absf(mean_b - b) <= tolerance;
	check(ok, string("v22 fixture: exported base %.0f %.0f %.0f (expected %.0f %.0f %.0f)", mean_r, mean_g, mean_b, r, g, b));
}

// Runs step st, returns the frames to wait before the next one or -1 after the last step
int run_step(int st) {
	if (st == 0) {
		step_build();
		return 2;
	}
	else if (st == 1) {
		step_remove_cube();
		return 2;
	}
	else if (st == 2) {
		step_layers();
		return 0;
	}
	else if (st == 3) {
		step_include();
		return 0;
	}
	else if (st == 4) {
		step_undo();
		return 0;
	}
	else if (st == 5) {
		step_exclude();
		return 0;
	}
	else if (st == 6) {
		step_viewport_include();
		return 0;
	}
	else if (st == 7) {
		step_viewport_exclude();
		return 0;
	}
	else if (st == 8) {
		step_tiles_filter();
		return 0;
	}
	else if (st == 9) {
		step_save();
		return 0;
	}
	else if (st == 10) {
		// project_save() writes on this frame
		return 0;
	}
	else if (st == 11) {
		step_reopen();
		return 2;
	}
	else if (st == 12) {
		step_group();
		return 0;
	}
	else if (st == 13) {
		step_flatten();
		return 0;
	}
	else if (st == 14) {
		step_merge();
		return 0;
	}
	else if (st == 15) {
		step_paint();
		return 0;
	}
	else if (st == 16) {
		step_tiles_build();
		return 2;
	}
	else if (st == 17) {
		step_remove_cube();
		return 2;
	}
	else if (st == 18) {
		step_tiles();
		return 0;
	}
	else if (st == 19) {
		step_v22_fixture();
		return 0;
	}
	return -1;
}

void next_frame() {
	if (wait > 0) {
		wait -= 1;
		script_notify_on_next_frame(next_frame);
		return;
	}
	step_again = 0;
	int w      = run_step(step);
	if (w < 0) {
		printf("[smoke] %d checks, %d failures", checks, failures);
		printf(failures == 0 && checks > 0 ? "SMOKE PASS" : "SMOKE FAIL");
		script_quit();
		return;
	}
	if (step_again) {
		sub += 1;
	}
	else {
		sub  = 0;
		wait = w;
		step += 1;
	}
	script_notify_on_next_frame(next_frame);
}

void main() {
	file_arm = string("%s/p1_geometry_mask.arm", dir_out);
	dir_exp  = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	script_notify_on_next_frame(next_frame);
}
