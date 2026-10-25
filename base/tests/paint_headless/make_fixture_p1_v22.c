#include "global.h"

// Builds fixtures/p1_v22_project.arm, a project of format version 22 saved before the
// geometry mask (LAY-11) added its layer fields, and the expected values of it in
// fixtures/p1_v22_project.json. p1_geometry_mask.c opens it to check that such projects
// keep opening unchanged. It was made with the build of commit 013316c, a newer build
// would save the geometry mask fields too and the fixture would no longer test that.
// Run with: base/tests/paint_headless/run.sh make_fixture_p1_v22.c

char *root       = "../../../base/tests/paint_headless";
char *dir_out    = "../smoke";
char *file_arm   = NULL;
char *file_json  = NULL;
char *dir_export = NULL;

int step   = 0;
int failed = 0;

void fail(char *what) {
	failed = 1;
	printf("[fixture] FAIL %s", what);
}

slot_layer_t *new_layer(char *name, int color, float rough) {
	slot_layer_t *l = script_layer_new_color(color, 1.0, rough, 0.0);
	if (l == NULL) {
		fail(string("layer %s", name));
		return NULL;
	}
	script_layer_set_name(l, name);
	return l;
}

void build() {
	if (script_get_config()->tools != TOOLS_PAINT) {
		fail("not in paint mode, remove data/config.json");
	}
	script_project_new();

	slot_layer_t *red = new_layer("v22_red", 0xffff0000, 0.3);
	script_layer_set_locked(red, true);

	slot_layer_t *green = new_layer("v22_green", 0xff00ff00, 0.9);
	slot_layer_t *mask  = script_layer_new_mask(green, 0);
	if (mask == NULL) {
		fail("black mask");
	}
	else {
		script_layer_set_name(mask, "v22_mask");
		mask->mask_opacity = 0.5;
	}

	slot_layer_t *blue  = new_layer("v22_blue", 0xff0000ff, 0.6);
	slot_layer_t *group = script_layer_new_group(blue);
	if (group == NULL) {
		fail("group");
	}
	else {
		script_layer_set_name(group, "v22_group");
	}
	script_layer_set_channel(blue, LAYER_CH_BASE, BLEND_TYPE_MIX, 0.5);

	if (script_layer_count() != 6) {
		fail(string("layer count %d", script_layer_count()));
	}
}

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

// Mean of the r, g, b channels of the exported base color
void base_mean(float *out) {
	out[0]             = -1.0;
	out[1]             = -1.0;
	out[2]             = -1.0;
	any_array_t *files = file_read_directory(dir_export);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, "_base.png")) {
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

	float m[3];
	base_mean(m);
	printf("[fixture] _base mean %.2f %.2f %.2f", m[0], m[1], m[2]);

	// Objects only, json_parse_to_map() skips arrays
	json_encode_begin();
	json_encode_string("fixture", "p1_v22_project.arm");
	json_encode_i32("layer_count", script_layer_count());
	json_encode_begin_object_key("layers");
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		json_encode_begin_object_key(l->name);
		json_encode_i32("index", i);
		json_encode_i32("locked", l->locked);
		json_encode_f32("opacity", l->mask_opacity);
		json_encode_string("parent", l->parent != NULL ? l->parent->name : "");
		json_encode_end_object();
	}
	json_encode_end_object();
	json_encode_begin_object_key("base");
	json_encode_f32("r", m[0]);
	json_encode_f32("g", m[1]);
	json_encode_f32("b", m[2]);
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
	file_arm   = string("%s/fixtures/p1_v22_project.arm", root);
	file_json  = string("%s/fixtures/p1_v22_project.json", root);
	dir_export = string("%s/fixture_v22_export", dir_out);
	script_notify_on_next_frame(next_frame);
}
