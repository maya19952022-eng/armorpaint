#include "global.h"

// BAKE-04: auto cage, rays start just past the high poly above the low poly instead of at a fixed distance.
// Run with: base/tests/paint_headless/run.sh p2_auto_cage.c

char *dir_out   = "../smoke";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

int RES = 64;

// A flat low poly plate, a high poly plate tilted by 20 degrees around y that dips below the low poly up to x 0.5 and
// rises above it after, and a high poly plane facing up above everything. Obj v is flipped on import.
char *mesh_scene = "o plate_low\n"
                   "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                   "vt 0.05 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.05 0.95\n"
                   "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                   "o plate_high\n"
                   "v 0 0 -0.181985\nv 0.75 0 0.090993\nv 0.75 1 0.090993\nv 0 1 -0.181985\n"
                   "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "f 5/5 6/6 7/7\nf 5/5 7/7 8/8\n"
                   "o cover_high\n"
                   "v 0 0 0.15\nv 1 0 0.15\nv 1 1 0.15\nv 0 1 0.15\n"
                   "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "f 9/9 10/10 11/11\nf 9/9 11/11 12/12\n";

int COL_BELOW = 16; // x 0.23, the tilted plate is below the low poly
int COL_ABOVE = 44; // x 0.72, the tilted plate is above the low poly
int COL_COVER = 58; // x 0.96, only the cover
int ROW       = 32;

float tilt_x = -0.34202; // Tilted plate normal, sin and cos of 20 degrees
float tilt_z = 0.93969;

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

float px(int slot, int x, int y, int c) {
	f32_array_t *p = script_mesh_map_pixels(slot);
	if (p == NULL) {
		return -1.0;
	}
	return p->buffer[(y * RES + x) * 4 + c];
}

int near3(int slot, int x, int y, float r, float g, float b, float tol) {
	return absf(px(slot, x, y, 0) - r) <= tol && absf(px(slot, x, y, 1) - g) <= tol && absf(px(slot, x, y, 2) - b) <= tol;
}

char *rgb(int slot, int x, int y) {
	return string("%.2f %.2f %.2f", px(slot, x, y, 0), px(slot, x, y, 1), px(slot, x, y, 2));
}

void bake() {
	script_bake_get_settings()->maps = (1 << MESH_MAP_WORLD_NORMAL) | (1 << MESH_MAP_OPACITY);
	script_bake_selected();
}

void step_build() {
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	check(script_get_object("plate_low") != NULL && script_get_object("cover_high") != NULL, "test scene appended");

	bake_settings_t *s = script_bake_get_settings();
	check(s->cage == BAKE_CAGE_DISTANCE, "the cage is a fixed distance by default");
	s->res            = RES;
	s->dilation       = 0;
	s->front_distance = 0.12; // The fixed cage starts above the cover
	s->rear_distance  = 0.1;
	script_bake_set_high_poly(script_get_object("plate_high"), true);
	script_bake_set_high_poly(script_get_object("cover_high"), true);
	bake();
}

void check_fixed() {
	check(near3(MESH_MAP_WORLD_NORMAL, COL_BELOW, ROW, 0.5, 0.5, 1.0, 0.02) && near3(MESH_MAP_WORLD_NORMAL, COL_ABOVE, ROW, 0.5, 0.5, 1.0, 0.02),
	      string("fixed cage: the rays meet the cover first (%s / %s)", rgb(MESH_MAP_WORLD_NORMAL, COL_BELOW, ROW),
	             rgb(MESH_MAP_WORLD_NORMAL, COL_ABOVE, ROW)));
	script_bake_get_settings()->cage = BAKE_CAGE_AUTO;
	check(script_bake_outdated(MESH_MAP_WORLD_NORMAL), "changing the cage makes the maps outdated");
	bake();
}

void check_auto() {
	float wx = tilt_x * 0.5 + 0.5;
	float wz = tilt_z * 0.5 + 0.5;
	check(near3(MESH_MAP_WORLD_NORMAL, COL_BELOW, ROW, wx, 0.5, wz, 0.02),
	      string("auto cage below: the plate under the low poly (%s, expected %.2f 0.50 %.2f)", rgb(MESH_MAP_WORLD_NORMAL, COL_BELOW, ROW), wx, wz));
	check(near3(MESH_MAP_WORLD_NORMAL, COL_ABOVE, ROW, wx, 0.5, wz, 0.02),
	      string("auto cage above: the cage stops past the plate, under the cover (%s)", rgb(MESH_MAP_WORLD_NORMAL, COL_ABOVE, ROW)));
	check(near3(MESH_MAP_WORLD_NORMAL, COL_COVER, ROW, 0.5, 0.5, 1.0, 0.02) && px(MESH_MAP_OPACITY, COL_COVER, ROW, 0) > 0.99,
	      string("auto cage: the cover is found where it is the only high poly (%s)", rgb(MESH_MAP_WORLD_NORMAL, COL_COVER, ROW)));

	// The search for the surface above stops at Max Frontal Distance
	script_bake_get_settings()->front_distance = 0.02;
	bake();
}

void check_auto_limit() {
	check(px(MESH_MAP_OPACITY, COL_ABOVE, ROW, 0) < 0.01, "auto cage does not search past Max Frontal Distance");
	check(near3(MESH_MAP_WORLD_NORMAL, COL_BELOW, ROW, tilt_x * 0.5 + 0.5, 0.5, tilt_z * 0.5 + 0.5, 0.02),
	      "the plate below is still found within the rear distance");

	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	script_project_new();
	check(script_bake_get_settings()->cage == BAKE_CAGE_DISTANCE, "a new project has the fixed cage");
	script_project_open(file_save);
	check(script_bake_get_settings()->cage == BAKE_CAGE_AUTO, "the auto cage survives save and load");
}

int waits_for_bake(int s) {
	return s >= 1 && s <= 3;
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
		check_fixed();
	}
	else if (step == 2) {
		check_auto();
	}
	else if (step == 3) {
		check_auto_limit();
	}
	else if (step == 4) {
		// project_save() runs on this frame
	}
	else if (step == 5) {
		step_reopen();
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
	file_save = string("%s/p2_auto_cage.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
