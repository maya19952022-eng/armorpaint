#include "global.h"

// BAKE-06: skew map painting, base normal and edge protection.
// Run with: base/tests/paint_headless/run.sh p2_skew_map.c

char *dir_out   = "../smoke";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

int RES = 64;

// A roof with a 90 degree ridge, a uv island per slope. The high poly is a plane 0.1 above the left slope, parallel to it.
// Near the ridge the smooth normals lean toward the ridge, so their rays meet the plane further up the slope than the
// rays along the triangle normal. Obj v is flipped on import.
char *mesh_scene = "o roof_low\n"
                   "v 0 0 0\nv 1 0 1\nv 1 1 1\nv 0 1 0\nv 2 0 0\nv 2 1 0\n"
                   "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.95\nvt 0.05 0.95\n"
                   "vt 0.55 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.55 0.95\n"
                   "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                   "f 2/5 5/6 6/7\nf 2/5 6/7 3/8\n"
                   "o plane_high\n"
                   "v -0.370711 0 -0.229289\nv 1.229289 0 1.370711\nv 1.229289 1 1.370711\nv -0.370711 1 -0.229289\n"
                   "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "f 7/9 8/10 9/11\nf 7/9 9/11 10/12\n";

int COL = 26; // u 0.414, 91% of the way up the left slope
int ROW = 32;

float X_MIN    = -0.370711; // Bounds of the scene along x
float X_EXTENT = 2.370711;

float x_smooth = 0.0;
float x_skew   = 0.0;

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

// Position map x at the test texel, 0 to 1 over the bounds
float hit_x() {
	f32_array_t *p = script_mesh_map_pixels(MESH_MAP_POSITION);
	if (p == NULL) {
		return -1.0;
	}
	return p->buffer[(ROW * RES + COL) * 4];
}

void bake() {
	script_bake_get_settings()->maps = 1 << MESH_MAP_POSITION;
	script_bake_selected();
}

slot_layer_t *find_role(int role) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->bake_role == role) {
			return l;
		}
	}
	return NULL;
}

void step_build() {
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	check(script_get_object("roof_low") != NULL && script_get_object("plane_high") != NULL, "test scene appended");

	bake_settings_t *s = script_bake_get_settings();
	check(s->skew_base == BAKE_SKEW_BASE_MESH && s->edge_protection && s->paint_value == 1.0, "default skew settings");
	s->res             = RES;
	s->dilation        = 0;
	s->front_distance  = 0.1;
	s->rear_distance   = 0.05;
	s->edge_protection = false;
	script_bake_set_high_poly(script_get_object("plane_high"), true);
	bake();
}

void check_smooth() {
	x_smooth = hit_x();
	// The triangle normal meets the plane 0.0707 back along x from the low poly point at x 0.910
	x_skew = (0.910156 - 0.0707107 - X_MIN) / X_EXTENT;
	check(x_smooth > x_skew + 0.015, string("without a skew map the rays follow the smooth normals (%.4f, triangle normal %.4f)", x_smooth, x_skew));
	check(find_role(BAKE_ROLE_SKEW) == NULL, "no skew map yet");

	int           layers = script_layer_count();
	slot_layer_t *l      = script_bake_paint_role(BAKE_ROLE_SKEW);
	check(l != NULL && l->bake_role == BAKE_ROLE_SKEW && script_layer_count() == layers + 1, "paint makes the skew map layer");
	check(script_get_context()->layer == l && script_get_context()->viewport_mode == VIEWPORT_MODE_MASK, "the skew map is selected and shown in the mask view");
	check(script_bake_paint_role(BAKE_ROLE_SKEW) == l && script_layer_count() == layers + 1, "a second paint selects the same layer");
}

void check_empty() {
	check(!script_bake_outdated(MESH_MAP_POSITION), "an empty skew map changes nothing, also once it was read back");
	script_bake_role_fill(BAKE_ROLE_SKEW, 1.0);
	check(script_bake_outdated(MESH_MAP_POSITION), "filling the skew map makes the maps outdated");
	bake();
}

void check_skewed() {
	float x = hit_x();
	check(absf(x - x_skew) < 0.004, string("a full skew map turns the rays to the triangle normal (%.4f, expected %.4f)", x, x_skew));
	script_bake_get_settings()->edge_protection = true;
	bake();
}

void check_protected() {
	float x = hit_x();
	check(x > x_skew + 0.004 && x < x_smooth - 0.004,
	      string("edge protection keeps the smooth normals near the ridge (%.4f between %.4f and %.4f)", x, x_skew, x_smooth));
	bake_settings_t *s = script_bake_get_settings();
	s->edge_protection = false;
	s->skew_base       = BAKE_SKEW_BASE_TRIANGLE;
	script_bake_role_fill(BAKE_ROLE_SKEW, -1.0); // Cleared
	bake();
}

void check_triangle_base() {
	float x = hit_x();
	check(absf(x - x_skew) < 0.004, string("base normal per triangle without skew: the triangle normal (%.4f)", x));
	script_bake_role_fill(BAKE_ROLE_SKEW, 1.0);
	bake();
}

void check_triangle_skewed() {
	float x = hit_x();
	check(absf(x - x_smooth) < 0.004, string("base normal per triangle with a full skew map: the smooth normals (%.4f)", x));

	// The skew map is left out of the material: selecting another layer brings the previous view back
	script_layer_set(script_layer_at(0));
	check(script_get_context()->viewport_mode != VIEWPORT_MODE_MASK, "selecting another layer leaves the mask view");

	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	script_project_new();
	check(find_role(BAKE_ROLE_SKEW) == NULL, "a new project has no skew map");
	script_project_open(file_save);
	bake_settings_t *s = script_bake_get_settings();
	check(find_role(BAKE_ROLE_SKEW) != NULL, "the skew map layer survives save and load");
	check(s->skew_base == BAKE_SKEW_BASE_TRIANGLE && !s->edge_protection, "skew settings survive save and load");
}

void check_reopened() {
	check(!script_bake_outdated(MESH_MAP_POSITION), "the reopened map is up to date with the reopened skew map");
}

int waits_for_bake(int s) {
	return s == 1 || (s >= 3 && s <= 6);
}

void next_frame() {
	if (waits_for_bake(step) && script_bake_busy()) {
		waited += 1;
		if (waited > 3000) {
			check(0, "bake finished in time");
			script_bake_cancel();
		}
		script_notify_on_next_frame(next_frame);
		return;
	}
	waited = 0;

	if (step == 0) {
		step_build();
	}
	else if (step == 1) {
		check_smooth();
	}
	else if (step == 2) {
		check_empty();
	}
	else if (step == 3) {
		check_skewed();
	}
	else if (step == 4) {
		check_protected();
	}
	else if (step == 5) {
		check_triangle_base();
	}
	else if (step == 6) {
		check_triangle_skewed();
	}
	else if (step == 7) {
		// project_save() runs on this frame
	}
	else if (step == 8) {
		step_reopen();
	}
	else if (step == 9) {
		// The role layers are read back on the next frame
	}
	else if (step == 10) {
		check_reopened();
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
	file_save = string("%s/p2_skew_map.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
