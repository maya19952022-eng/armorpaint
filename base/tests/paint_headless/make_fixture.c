#include "global.h"

// Builds the fixture project used by run.c: fixtures/p0_project.arm and the
// per-channel means of its exported textures in fixtures/p0_project.json.
// The fixture is saved with the layer format of phase P0, later phases must
// keep opening it with the same result (see readme.md).
// Run from paint/build/out:
//   ./ArmorPaint --background --script ../../../base/tests/paint_headless/make_fixture.c

char *root        = "../../../base/tests/paint_headless";
char *dir_out     = "../smoke";
char *file_arm    = NULL;
char *file_json   = NULL;
char *dir_export  = NULL;
char *channels[5] = {"_base", "_occ", "_rough", "_metal", "_nor"}; // Suffixes of the generic export preset

int step   = 0;
int failed = 0;

// blend_type_t values, the enum is not exposed to scripts
int BLEND_MIX      = 0;
int BLEND_MULTIPLY = 2;
int BLEND_ADD      = 7;
int BLEND_OVERLAY  = 8;

void fail(char *what) {
	failed = 1;
	printf("[fixture] FAIL %s", what);
}

slot_layer_t *new_layer(char *name, int color, float occ, float rough, float met) {
	slot_layer_t *l = script_layer_new_color(color, occ, rough, met);
	if (l == NULL) {
		fail(string("layer %s", name));
		return NULL;
	}
	script_layer_set_name(l, name);
	return l;
}

void build() {
	// A config left in sculpt mode adds a sculpt layer, run.sh starts from the defaults
	if (script_get_config()->tools != TOOLS_PAINT) {
		fail("not in paint mode, remove data/config.json");
	}
	script_project_new();

	slot_layer_t *gray = new_layer("p0_gray", 0xff808080, 1.0, 0.5, 0.0);

	slot_layer_t *mul = new_layer("p0_multiply", 0xffcc3311, 0.9, 0.8, 1.0);
	mul->blending     = BLEND_MULTIPLY;
	mul->mask_opacity = 0.5;
	script_layer_update(mul);

	slot_layer_t *add = new_layer("p0_add", 0xff1144cc, 0.5, 0.2, 0.0);
	add->blending     = BLEND_ADD;
	add->mask_opacity = 0.75;
	script_layer_update(add);
	slot_layer_t *group = script_layer_new_group(add);
	if (group == NULL) {
		fail("group");
	}
	else {
		script_layer_set_name(group, "p0_group");
		group->mask_opacity = 0.8;
		script_layer_update(group);
	}

	slot_layer_t *masked = new_layer("p0_masked", 0xff22cc22, 1.0, 0.3, 0.0);
	masked->blending     = BLEND_OVERLAY;
	script_layer_update(masked);
	slot_layer_t *mask = script_layer_new_mask(masked, 0);
	if (mask == NULL) {
		fail("black mask");
	}
	else {
		script_layer_set_name(mask, "p0_mask");
	}

	slot_layer_t *hidden = new_layer("p0_hidden", 0xffffffff, 1.0, 1.0, 1.0);
	hidden->visible      = 0;
	script_layer_update(hidden);

	if (gray == NULL || script_layer_count() < 7) {
		fail(string("layer count %d", script_layer_count()));
	}
}

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

// Mean of the r, g, b channels of an exported texture, -1 when it was not exported
void channel_mean(char *suffix, float *out) {
	out[0]             = -1.0;
	out[1]             = -1.0;
	out[2]             = -1.0;
	any_array_t *files = file_read_directory(dir_export);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, string("%s.png", suffix))) {
			continue;
		}
		script_import_asset(string("%s/%s", dir_export, name), false);
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

void write_expected() {
	iron_create_directory(dir_out);
	iron_create_directory(dir_export);
	clear_dir(dir_export);
	export_texture_run(dir_export, false);

	// Objects only, json_parse_to_map() skips arrays
	json_encode_begin();
	json_encode_string("fixture", "p0_project.arm");
	json_encode_i32("layer_count", script_layer_count());
	json_encode_begin_object_key("layers");
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		json_encode_begin_object_key(l->name);
		json_encode_i32("index", i);
		json_encode_i32("blending", l->blending);
		json_encode_f32("opacity", l->mask_opacity);
		json_encode_i32("visible", l->visible);
		json_encode_string("parent", l->parent != NULL ? l->parent->name : "");
		json_encode_end_object();
	}
	json_encode_end_object();
	json_encode_begin_object_key("channels");
	for (int i = 0; i < 5; ++i) {
		float m[3];
		channel_mean(channels[i], m);
		json_encode_begin_object_key(channels[i]);
		json_encode_f32("r", m[0]);
		json_encode_f32("g", m[1]);
		json_encode_f32("b", m[2]);
		json_encode_end_object();
		printf("[fixture] %s mean %.2f %.2f %.2f", channels[i], m[0], m[1], m[2]);
	}
	json_encode_end_object();
	char *json = json_encode_end();
	iron_file_save_bytes(file_json, sys_string_to_buffer(json), string_length(json));
}

void next_frame() {
	if (step == 0) {
		build();
	}
	else if (step == 1) {
		write_expected();
		project_filepath_set(file_arm);
		project_save(false);
	}
	else if (step == 2) {
		// project_save() writes on the next frame
	}
	else {
		if (!iron_file_exists(file_arm)) {
			fail("fixture not saved");
		}
		printf(failed ? "FIXTURE FAIL" : "FIXTURE DONE %s", file_arm);
		script_quit();
		return;
	}
	step += 1;
	script_notify_on_next_frame(next_frame);
}

void main() {
	file_arm   = string("%s/fixtures/p0_project.arm", root);
	file_json  = string("%s/fixtures/p0_project.json", root);
	dir_export = string("%s/fixture_export", dir_out);
	script_notify_on_next_frame(next_frame);
}
