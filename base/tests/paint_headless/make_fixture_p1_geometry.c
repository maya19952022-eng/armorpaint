#include "global.h"

// Builds fixtures/p1_geometry_project.arm, a project of format version 22 saved with the geometry
// mask of objects and tiles (44 keys per layer) before the face selection of the geometry mask
// added its layer fields, and the expected values of it in fixtures/p1_geometry_project.json.
// p1_geometry_faces.c opens it to check that such projects keep opening unchanged. It was made
// with the build of commit 8a03c95, a newer build would save the face fields too and the fixture
// would no longer test that.
// Run with: base/tests/paint_headless/run.sh make_fixture_p1_geometry.c

char *root       = "../../../base/tests/paint_headless";
char *dir_out    = "../smoke";
char *file_arm   = NULL;
char *file_json  = NULL;
char *dir_export = NULL;

int step   = 0;
int wait   = 0;
int failed = 0;

object_t *cube = NULL;

// Quads facing the default camera, geo_a in the left half of the uv space and geo_b in the right half
char *mesh_objects = "o geo_a\n"
                     "v -1.0 0.0 -0.5\nv -0.1 0.0 -0.5\nv -0.1 0.0 0.5\nv -1.0 0.0 0.5\n"
                     "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.95\nvt 0.05 0.95\n"
                     "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                     "o geo_b\n"
                     "v 0.1 0.0 -0.5\nv 1.0 0.0 -0.5\nv 1.0 0.0 0.5\nv 0.1 0.0 0.5\n"
                     "vt 0.55 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.55 0.95\n"
                     "f 5/5 6/6 7/7\nf 5/5 7/7 8/8\n";

char *names[6] = {"Layer 1", "fx_red", "fx_blue", "fx_green", "fx_group", NULL};

void fail(char *what) {
	failed = 1;
	printf("[fixture] FAIL %s", what);
}

slot_layer_t *new_layer(char *name, int color) {
	slot_layer_t *l = script_layer_new_color(color, 1.0, 0.5, 0.0);
	if (l == NULL) {
		fail(string("layer %s", name));
		return NULL;
	}
	script_layer_set_name(l, name);
	return l;
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

void build() {
	if (script_get_config()->tools != TOOLS_PAINT) {
		fail("not in paint mode, remove data/config.json");
	}
	script_project_new();
	cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_objects);
	if (script_get_object("geo_a") == NULL || script_get_object("geo_b") == NULL) {
		fail("quads appended");
	}
}

void build_layers() {
	object_t *a = script_get_object("geo_a");
	object_t *b = script_get_object("geo_b");

	// Red on geo_a only
	slot_layer_t *red = new_layer("fx_red", 0xffff0000);
	script_layer_set_geometry_object(red, a, true);

	// Blue at half opacity everywhere but tile 1002, which has no object: it stays everywhere
	slot_layer_t *blue = new_layer("fx_blue", 0xff0000ff);
	blue->mask_opacity = 0.5;
	script_layer_update(blue);
	script_layer_set_geometry_tile(blue, 1002, true);
	script_layer_set_geometry_mode(blue, GEOMETRY_MASK_EXCLUDE);

	// Green in a group that includes geo_b
	slot_layer_t *green = new_layer("fx_green", 0xff00ff00);
	slot_layer_t *group = script_layer_new_group(green);
	if (group == NULL) {
		fail("group");
		return;
	}
	script_layer_set_name(group, "fx_group");
	script_layer_set_geometry_object(group, b, true);

	if (script_layer_count() != 5) {
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

	object_t *a = script_get_object("geo_a");
	object_t *b = script_get_object("geo_b");

	// Objects only, json_parse_to_map() skips arrays
	json_encode_begin();
	json_encode_string("fixture", "p1_geometry_project.arm");
	json_encode_i32("layer_count", script_layer_count());
	json_encode_begin_object_key("layers");
	for (int i = 0; names[i] != NULL; ++i) {
		slot_layer_t *l = layer_by_name(names[i]);
		if (l == NULL) {
			fail(string("layer %s missing", names[i]));
			continue;
		}
		json_encode_begin_object_key(l->name);
		json_encode_i32("index", script_layer_index(l));
		json_encode_i32("mode", script_layer_get_geometry_mode(l));
		json_encode_i32("geo_a", script_layer_get_geometry_object(l, a));
		json_encode_i32("geo_b", script_layer_get_geometry_object(l, b));
		json_encode_i32("tile_1002", script_layer_get_geometry_tile(l, 1002));
		json_encode_i32("applies_a", script_layer_geometry_applies(l, a));
		json_encode_i32("applies_b", script_layer_geometry_applies(l, b));
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
	if (wait > 0) {
		wait -= 1;
		script_notify_on_next_frame(next_frame);
		return;
	}
	if (step == 0) {
		build();
		wait = 2;
	}
	else if (step == 1) {
		script_object_remove(cube);
		wait = 2;
	}
	else if (step == 2) {
		build_layers();
	}
	else if (step == 3) {
		write_expected();
		project_filepath_set(file_arm);
		project_save(false);
	}
	else if (step == 4) {
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
	file_arm   = string("%s/fixtures/p1_geometry_project.arm", root);
	file_json  = string("%s/fixtures/p1_geometry_project.json", root);
	dir_export = string("%s/fixture_geometry_export", dir_out);
	script_notify_on_next_frame(next_frame);
}
