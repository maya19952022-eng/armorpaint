#include "global.h"

// VIEW-10: mesh maps and the directions of the rays to the high poly as viewport modes.
// Run with: base/tests/paint_headless/run.sh p2_mesh_map_view.c

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;
int slot     = 0;

double time0 = 0.0;

// The roof of p2_skew_map.c: smooth normals lean toward the ridge, the triangle normal of the left slope does not
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

int RES = 512; // Of the ray directions
int COL = 212; // u 0.415, near the ridge on the left slope
int ROW = 256;

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

float dir(int c) {
	f32_array_t *p = script_bake_ray_directions_pixels();
	if (p == NULL) {
		return -1.0;
	}
	return p->buffer[(ROW * RES + COL) * 4 + c] * 2.0 - 1.0;
}

void step_build() {
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	bake_settings_t *s = script_bake_get_settings();
	s->res             = 64;
	s->edge_protection = false;
	script_bake_set_high_poly(script_get_object("plane_high"), true);
	s->maps = (1 << MESH_MAP_OCCLUSION) | (1 << MESH_MAP_CURVATURE);
	script_bake_selected();
}

// Every slot as a viewport mode, baked or not, one per frame so each shader is drawn
void show_slot() {
	script_set_viewport_mesh_map(slot);
	check(script_get_context()->viewport_mode == VIEWPORT_MODE_MESH_MAP && script_get_context()->viewport_mesh_map == slot,
	      string("viewport shows mesh map %d", slot));
	slot += 1;
}

void show_directions() {
	script_set_viewport_mesh_map(MESH_MAP_COUNT);
	check(script_get_context()->viewport_mesh_map == MESH_MAP_COUNT, "viewport shows the ray directions");
	time0 = sys_time();
}

void check_smooth() {
	// The smooth normals near the ridge lean toward it: almost straight up
	check(dir(0) < 0.0 && dir(0) > -0.3 && dir(2) > 0.9, string("ray directions follow the smooth normals (%.2f %.2f %.2f)", dir(0), dir(1), dir(2)));
	script_bake_paint_role(BAKE_ROLE_SKEW);
	script_bake_role_fill(BAKE_ROLE_SKEW, 1.0);
	script_set_viewport_mesh_map(MESH_MAP_COUNT); // The skew map was shown while selected
	time0 = sys_time();
}

void check_skewed() {
	check(absf(dir(0) + 0.707) < 0.03 && absf(dir(2) - 0.707) < 0.03,
	      string("with the skew map the directions follow the triangle normal (%.2f %.2f %.2f)", dir(0), dir(1), dir(2)));
	script_set_viewport_mesh_map(MESH_MAP_COUNT + 5);
	check(script_get_context()->viewport_mesh_map == MESH_MAP_COUNT, "a slot past the ray directions is clamped");
}

int waiting() {
	if (step == 1) {
		return script_bake_busy();
	}
	if (step == 12) { // Made within a quarter second
		return script_bake_ray_directions_pixels() == NULL && sys_time() - time0 < 3.0;
	}
	if (step == 13) {
		return dir(0) > -0.6 && sys_time() - time0 < 3.0;
	}
	return 0;
}

void next_frame() {
	if (waiting()) {
		waited += 1;
		script_notify_on_next_frame(next_frame);
		return;
	}
	waited = 0;

	if (step == 0) {
		step_build();
	}
	else if (step >= 1 && step <= 10) {
		if (step <= MESH_MAP_COUNT) {
			show_slot();
		}
		else {
			show_directions();
		}
	}
	else if (step == 11) {
		// The directions are made by the update of this frame
	}
	else if (step == 12) {
		check(script_bake_ray_directions_pixels() != NULL, "ray directions are made when shown");
		check_smooth();
	}
	else if (step == 13) {
		check_skewed();
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
	script_notify_on_next_frame(next_frame);
}
