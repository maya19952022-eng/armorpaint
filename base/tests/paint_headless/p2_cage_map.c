#include "global.h"

// BAKE-07: cage map painting, a painted offset over the cage per texel.
// Run with: base/tests/paint_headless/run.sh p2_cage_map.c

char *dir_out   = "../smoke";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

int RES = 32;

// A low poly plate and a high poly plate 0.05 above it, past the cage of a small Max Frontal Distance
char *mesh_scene = "o plate_low\n"
                   "v 0 0 0\nv 1 0 0\nv 1 1 0\nv 0 1 0\n"
                   "vt 0.05 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.05 0.95\n"
                   "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                   "o plate_high\n"
                   "v 0 0 0.05\nv 1 0 0.05\nv 1 1 0.05\nv 0 1 0.05\n"
                   "vt 0 0\nvt 1 0\nvt 1 1\nvt 0 1\n"
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

float opacity() {
	f32_array_t *p = script_mesh_map_pixels(MESH_MAP_OPACITY);
	if (p == NULL) {
		return -1.0;
	}
	return p->buffer[(16 * RES + 16) * 4];
}

void bake() {
	script_bake_get_settings()->maps = 1 << MESH_MAP_OPACITY;
	script_bake_selected();
}

void step_build() {
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	check(script_get_object("plate_low") != NULL && script_get_object("plate_high") != NULL, "test scene appended");

	bake_settings_t *s = script_bake_get_settings();
	check(absf(s->cage_range - 0.05) < 0.0001, "default cage range");
	s->res            = RES;
	s->dilation       = 0;
	s->front_distance = 0.01; // 0.014 above the low poly, under the high poly
	s->rear_distance  = 0.01;
	s->cage_range     = 0.1;
	script_bake_set_high_poly(script_get_object("plate_high"), true);
	bake();
}

float absf(float f) {
	return f < 0.0 ? -f : f;
}

void check_unpainted() {
	check(opacity() < 0.01, "without a cage map the cage is under the high poly, the rays miss it");
	slot_layer_t *l = script_bake_paint_role(BAKE_ROLE_CAGE);
	check(l != NULL && l->bake_role == BAKE_ROLE_CAGE && script_get_context()->layer == l, "paint makes and selects the cage map layer");
	check(script_bake_paint_role(BAKE_ROLE_SKEW) != l, "the skew map is another layer");
	script_layer_set(l);
	script_bake_role_fill(BAKE_ROLE_CAGE, 1.0); // White: the cage moves out by the range
	check(script_bake_outdated(MESH_MAP_OPACITY), "filling the cage map makes the maps outdated");
	bake();
}

void check_pushed() {
	check(opacity() > 0.99, "a white cage map pushes the cage past the high poly");
	script_bake_role_fill(BAKE_ROLE_CAGE, 0.5); // Middle gray: no offset
	bake();
}

void check_neutral() {
	check(opacity() < 0.01, "a middle gray cage map leaves the cage where it was");
	script_bake_role_fill(BAKE_ROLE_CAGE, 1.0);
	script_bake_get_settings()->cage_range = 0.01; // 0.014 more, still under the high poly
	bake();
}

void check_range() {
	check(opacity() < 0.01, "the range scales the offset of white");
	script_bake_get_settings()->cage_range = 0.1;
	script_bake_role_fill(BAKE_ROLE_CAGE, -1.0); // Cleared: transparent is no offset
	bake();
}

void check_cleared() {
	check(opacity() < 0.01, "a cleared cage map has no offset");
	script_bake_role_fill(BAKE_ROLE_CAGE, 1.0);
	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	script_project_new();
	script_project_open(file_save);
	check(absf(script_bake_get_settings()->cage_range - 0.1) < 0.0001, "the cage range survives save and load");
	bake();
}

void check_reopened() {
	check(opacity() > 0.99, "the reopened cage map still pushes the cage");
}

int waits_for_bake(int s) {
	return (s >= 1 && s <= 5) || s == 8;
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
		check_unpainted();
	}
	else if (step == 2) {
		check_pushed();
	}
	else if (step == 3) {
		check_neutral();
	}
	else if (step == 4) {
		check_range();
	}
	else if (step == 5) {
		check_cleared();
	}
	else if (step == 6) {
		// project_save() runs on this frame
	}
	else if (step == 7) {
		step_reopen();
	}
	else if (step == 8) {
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
	file_save = string("%s/p2_cage_map.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
