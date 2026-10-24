#include "global.h"

// BAKE-03: mesh maps baked from high poly objects by rays from a cage above the low poly.
// Run with: base/tests/paint_headless/run.sh p2_high_poly.c

char *dir_out   = "../smoke";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

int RES = 64;

// A flat low poly plate. The high poly plate is tilted by 20 degrees around y and covers it up to x 0.75.
// A decoy plane faces down just above the low poly: a ray from the cage meets its back first.
// Obj v is flipped on import, texel rows run against y.
char *mesh_scene = "o plate_low\n"
                   "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                   "vt 0.05 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.05 0.95\n"
                   "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                   "o plate_high\n"
                   "v 0 0 -0.181985\nv 0.75 0 0.090993\nv 0.75 1 0.090993\nv 0 1 -0.181985\n"
                   "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
                   "f 5/5 6/6 7/7\nf 5/5 7/7 8/8\n"
                   "o decoy_high\n"
                   "v 0 0 0.05\nv 0 1 0.05\nv 1 1 0.05\nv 1 0 0.05\n"
                   "vt 0 0\nvt 0 1\nvt 1 1\nvt 1 0\n"
                   "f 9/9 10/10 11/11\nf 9/9 11/11 12/12\n";

int COL_TILTED = 17; // x 0.25, under the tilted part
int COL_BEYOND = 56; // x 0.92, past the end of the high poly plate
int COL_EDGE   = 46; // The end of the high poly plate crosses this texel at 37%
int ROW        = 32;

float tilt_x = -0.34202; // High poly normal, sin and cos of 20 degrees
float tilt_z = 0.93969;
int   id_decoy[3];

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

void bake(int maps) {
	script_bake_get_settings()->maps = maps;
	script_bake_selected();
}

int all_maps() {
	return (1 << MESH_MAP_NORMAL) | (1 << MESH_MAP_WORLD_NORMAL) | (1 << MESH_MAP_OPACITY) | (1 << MESH_MAP_ID) | (1 << MESH_MAP_POSITION);
}

void step_build() {
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	check(script_get_object("plate_low") != NULL && script_get_object("decoy_high") != NULL, "test scene appended");

	bake_settings_t *s = script_bake_get_settings();
	check(s->aa == 1 && s->average_normals && s->ignore_backface && !s->match_by_name && s->front_distance > 0.0 && s->rear_distance > 0.0,
	      "default high poly settings");
	s->res             = RES;
	s->dilation        = 0;
	s->front_distance  = 0.15;
	s->rear_distance   = 0.15;
	s->ignore_backface = false;
	script_bake_set_high_poly(script_get_object("plate_high"), true);
	script_bake_set_high_poly(script_get_object("decoy_high"), true);
	check(script_bake_is_high_poly(script_get_object("plate_high")) && !script_bake_is_high_poly(script_get_object("plate_low")), "high poly list");
	bake(all_maps());
}

void check_backface_hit() {
	// Without ignore backface the ray stops at the back of the decoy
	check(near3(MESH_MAP_WORLD_NORMAL, COL_TILTED, ROW, 0.5, 0.5, 0.0, 0.02), string("decoy hit from behind: world normal %s", rgb(MESH_MAP_WORLD_NORMAL, COL_TILTED, ROW)));
	check(px(MESH_MAP_OPACITY, COL_BEYOND, ROW, 0) > 0.99, "the decoy covers the whole plate");
	id_decoy[0] = (int)(px(MESH_MAP_ID, COL_TILTED, ROW, 0) * 255.0);
	id_decoy[1] = (int)(px(MESH_MAP_ID, COL_TILTED, ROW, 1) * 255.0);
	id_decoy[2] = (int)(px(MESH_MAP_ID, COL_TILTED, ROW, 2) * 255.0);
	script_bake_get_settings()->ignore_backface = true;
	bake(all_maps());
}

void check_tilted() {
	// The decoy faces away from the ray and is skipped, the tilted plate below is found
	float wx = tilt_x * 0.5 + 0.5;
	float wz = tilt_z * 0.5 + 0.5;
	check(near3(MESH_MAP_WORLD_NORMAL, COL_TILTED, ROW, wx, 0.5, wz, 0.02),
	      string("high poly world normal %s (expected %.2f 0.50 %.2f)", rgb(MESH_MAP_WORLD_NORMAL, COL_TILTED, ROW), wx, wz));
	// u runs along x and v along y on the low poly, so the tangent normal has the same x and z as the world one
	check(near3(MESH_MAP_NORMAL, COL_TILTED, ROW, wx, 0.5, wz, 0.02),
	      string("tangent space normal %s (expected %.2f 0.50 %.2f)", rgb(MESH_MAP_NORMAL, COL_TILTED, ROW), wx, wz));
	check(px(MESH_MAP_OPACITY, COL_TILTED, ROW, 0) > 0.99 && px(MESH_MAP_OPACITY, COL_BEYOND, ROW, 0) < 0.01, "opacity: high poly found, then missing");
	check(near3(MESH_MAP_NORMAL, COL_BEYOND, ROW, 0.5, 0.5, 1.0, 0.01), "flat tangent normal where no high poly is found");
	check(near3(MESH_MAP_WORLD_NORMAL, COL_BEYOND, ROW, 0.5, 0.5, 1.0, 0.02), "the low poly stands in where no high poly is found");
	// Bounds z from -0.18 to 0.09, the hit is at -0.09
	float z = px(MESH_MAP_POSITION, COL_TILTED, ROW, 2);
	check(absf(z - 0.333) < 0.03, string("position of the high poly hit z %.3f (expected 0.333)", z));
	int same_id = absf(px(MESH_MAP_ID, COL_TILTED, ROW, 0) * 255.0 - id_decoy[0]) < 2 && absf(px(MESH_MAP_ID, COL_TILTED, ROW, 1) * 255.0 - id_decoy[1]) < 2 &&
	              absf(px(MESH_MAP_ID, COL_TILTED, ROW, 2) * 255.0 - id_decoy[2]) < 2;
	check(!same_id, "id colors follow the high poly objects");

	// Match by name: plate_low only finds plate_high, objects ending in _high are high poly without the list
	bake_settings_t *s = script_bake_get_settings();
	script_bake_set_high_poly(script_get_object("plate_high"), false);
	script_bake_set_high_poly(script_get_object("decoy_high"), false);
	check(!script_bake_is_high_poly(script_get_object("plate_high")), "taken out of the high poly list");
	s->match_by_name   = true;
	s->ignore_backface = false;
	check(script_bake_is_high_poly(script_get_object("plate_high")) && script_bake_is_high_poly(script_get_object("decoy_high")) &&
	          !script_bake_is_high_poly(script_get_object("plate_low")),
	      "match by name: _high objects are high poly");
	bake(all_maps());
}

void check_match_by_name() {
	float wx = tilt_x * 0.5 + 0.5;
	float wz = tilt_z * 0.5 + 0.5;
	check(near3(MESH_MAP_WORLD_NORMAL, COL_TILTED, ROW, wx, 0.5, wz, 0.02),
	      string("match by name skips decoy_high: world normal %s", rgb(MESH_MAP_WORLD_NORMAL, COL_TILTED, ROW)));
	check(px(MESH_MAP_OPACITY, COL_EDGE, ROW, 0) < 0.01, "without antialiasing the edge texel center misses the plate");
	script_bake_get_settings()->aa = 4;
	bake(1 << MESH_MAP_OPACITY);
}

void check_antialiasing() {
	float edge = px(MESH_MAP_OPACITY, COL_EDGE, ROW, 0);
	check(absf(edge - 0.5) < 0.07, string("4x4 samples cover half of the edge texel: %.3f", edge));
	check(px(MESH_MAP_OPACITY, COL_TILTED, ROW, 0) > 0.99 && px(MESH_MAP_OPACITY, COL_BEYOND, ROW, 0) < 0.01, "full and empty texels stay full and empty");

	// The list follows a rename
	bake_settings_t *s = script_bake_get_settings();
	s->match_by_name   = false;
	script_bake_set_high_poly(script_get_object("decoy_high"), true);
	script_object_set_name(script_get_object("decoy_high"), "decoy_renamed");
	check(script_get_object("decoy_renamed") != NULL && script_bake_is_high_poly(script_get_object("decoy_renamed")), "a renamed high poly stays in the list");

	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	script_project_new();
	check(script_bake_get_settings()->aa == 1 && string_equals(script_bake_get_settings()->high_polys, ""), "a new project has no high poly");
	script_project_open(file_save);
	bake_settings_t *s = script_bake_get_settings();
	check(s->aa == 4 && absf(s->front_distance - 0.15) < 0.001 && absf(s->rear_distance - 0.15) < 0.001 && !s->ignore_backface && !s->match_by_name &&
	          s->average_normals,
	      "high poly settings survive save and load");
	check(script_bake_is_high_poly(script_get_object("decoy_renamed")) && !script_bake_is_high_poly(script_get_object("plate_high")),
	      string("high poly list survives save and load (%s)", s->high_polys));
}

int waits_for_bake(int s) {
	return s >= 1 && s <= 4;
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
		check_backface_hit();
	}
	else if (step == 2) {
		check_tilted();
	}
	else if (step == 3) {
		check_match_by_name();
	}
	else if (step == 4) {
		check_antialiasing();
	}
	else if (step == 5) {
		// project_save() runs on this frame
	}
	else if (step == 6) {
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
	file_save = string("%s/p2_high_poly.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
