#include "global.h"

// Headless smoke test: opens the fixture project, checks its layers and exported
// textures, adds a layer, saves, reopens the saved project and exports again.
// Prints "SMOKE PASS" or "SMOKE FAIL", see readme.md for how to run it.
// Paths are relative to paint/build/out, the working directory of the app.

char *root      = "../../../base/tests/paint_headless";
char *dir_out   = "../smoke";
char *file_arm  = NULL;
char *file_json = NULL;
char *file_save = NULL;
char *dir_exp   = NULL;

any_map_t *expected    = NULL;
char      *channels[5] = {"_base", "_occ", "_rough", "_metal", "_nor"}; // Suffixes of the generic export preset
float      tolerance   = 3.0;                                           // Per 8-bit channel, GPUs differ in rounding

int step         = 0;
int checks       = 0;
int failures     = 0;
int export_count = 0;

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

float expected_f(char *key) {
	char *s = any_map_get(expected, key);
	return s != NULL ? parse_float(s) : -1000.0;
}

int expected_i(char *key) {
	char *s = any_map_get(expected, key);
	return s != NULL ? parse_int(s) : -1000;
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

// Layers keep their order, blending, opacity, visibility and parent
void check_layers(char *when) {
	char *names[8] = {"Layer 1", "p0_gray", "p0_multiply", "p0_add", "p0_group", "p0_mask", "p0_masked", "p0_hidden"};
	for (int i = 0; i < 8; ++i) {
		char         *key = string("layers.%s", names[i]);
		slot_layer_t *l   = layer_by_name(names[i]);
		if (l == NULL) {
			check(0, string("%s: layer %s exists", when, names[i]));
			continue;
		}
		char *parent = l->parent != NULL ? l->parent->name : "";
		int   ok     = script_layer_index(l) == expected_i(string("%s.index", key)) && l->blending == expected_i(string("%s.blending", key)) &&
		         absf(l->mask_opacity - expected_f(string("%s.opacity", key))) < 0.001 && l->visible == expected_i(string("%s.visible", key)) &&
		         string_equals(parent, any_map_get(expected, string("%s.parent", key)));
		check(ok, string("%s: layer %s (index %d, blending %d, opacity %.2f, visible %d, parent '%s')", when, names[i], script_layer_index(l), l->blending,
		                 l->mask_opacity, l->visible, parent));
	}
}

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

// Mean of the r, g, b channels of an exported texture, -1 when it was not exported
void channel_mean(char *dir, char *suffix, float *out) {
	out[0]             = -1.0;
	out[1]             = -1.0;
	out[2]             = -1.0;
	any_array_t *files = file_read_directory(dir);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, string("%s.png", suffix))) {
			continue;
		}
		script_import_asset(string("%s/%s", dir, name), false);
		gpu_texture_t *tex    = script_get_texture(name);
		buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
		if (pixels != NULL) {
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
			out[0] = sum[0] / count;
			out[1] = sum[1] / count;
			out[2] = sum[2] / count;
		}
		script_texture_delete(name);
		return;
	}
}

// Exported textures match the means recorded when the fixture was made
void check_export(char *when) {
	export_count += 1;
	char *dir = string("%s/export%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	any_array_t *files = file_read_directory(dir);
	check(files != NULL && files->length >= 5, string("%s: textures exported (%d files)", when, files != NULL ? files->length : 0));

	for (int i = 0; i < 5; ++i) {
		float m[3];
		channel_mean(dir, channels[i], m);
		char *key = string("channels.%s", channels[i]);
		float r   = expected_f(string("%s.r", key));
		float g   = expected_f(string("%s.g", key));
		float b   = expected_f(string("%s.b", key));
		int   ok  = absf(m[0] - r) <= tolerance && absf(m[1] - g) <= tolerance && absf(m[2] - b) <= tolerance;
		check(ok, string("%s: %s mean %.1f %.1f %.1f (expected %.1f %.1f %.1f)", when, channels[i], m[0], m[1], m[2], r, g, b));
	}
}

void step_open() {
	buffer_t *b = script_load_blob(file_json);
	check(b != NULL, "expected values readable");
	if (b == NULL) {
		return;
	}
	expected = json_parse_to_map(sys_buffer_to_string(b));
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	check(iron_file_exists(file_arm), "fixture exists");
	script_project_open(file_arm);
	check(string_equals(project_filepath_get(), file_arm) || ends_with(project_filepath_get(), "p0_project.arm"), "fixture opened");
	check(script_layer_count() == expected_i("layer_count"), string("layer count %d (expected %d)", script_layer_count(), expected_i("layer_count")));
	check_layers("fixture");
}

void step_export_fixture() {
	check_export("fixture");
}

void step_edit_and_save() {
	// A transparent layer on top, the composite stays the same
	slot_layer_t *l = script_layer_new();
	check(l != NULL, "new layer created");
	if (l != NULL) {
		script_layer_set_name(l, "smoke_new");
	}
	iron_delete_file(file_save);
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	check(iron_file_exists(file_save), "project saved");
	script_project_new();
	check(layer_by_name("smoke_new") == NULL, "new project is empty");
	script_project_open(file_save);
	check(script_layer_count() == expected_i("layer_count") + 1, string("saved layer count %d", script_layer_count()));
	check(layer_by_name("smoke_new") != NULL, "new layer survived save and load");
	check_layers("resaved");
}

void step_export_resaved() {
	check_export("resaved");
}

void next_frame() {
	if (step == 0) {
		step_open();
	}
	else if (step == 1) {
		step_export_fixture();
	}
	else if (step == 2) {
		step_edit_and_save();
	}
	else if (step == 3) {
		// project_save() runs on this frame
	}
	else if (step == 4) {
		step_reopen();
	}
	else if (step == 5) {
		step_export_resaved();
	}
	else {
		printf("[smoke] %d checks, %d failures", checks, failures);
		printf(failures == 0 && checks > 0 ? "SMOKE PASS" : "SMOKE FAIL");
		script_quit();
		return;
	}
	step += 1;
	script_notify_on_next_frame(next_frame);
}

void main() {
	file_arm  = string("%s/fixtures/p0_project.arm", root);
	file_json = string("%s/fixtures/p0_project.json", root);
	file_save = string("%s/smoke_resaved.arm", dir_out);
	dir_exp   = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	printf("[smoke] output directory %s", dir_out);
	script_notify_on_next_frame(next_frame);
}
