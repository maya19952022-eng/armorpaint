#include "global.h"

// BAKE-05: rebaking only the texels of the objects that changed.
// Run with: base/tests/paint_headless/run.sh p2_partial_rebake.c

int step     = 0;
int checks   = 0;
int failures = 0;

int RES  = 64;
int full = 0; // Texels of a full bake

f32_array_t *partial_px = NULL;

// Two plates facing up, a uv half each. Turning plate_a over around x keeps the bounds of the scene.
char *mesh_scene = "o plate_a\n"
                   "v -2 -0.5 0\nv -1 -0.5 0\nv -1 0.5 0\nv -2 0.5 0\n"
                   "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.95\nvt 0.05 0.95\n"
                   "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                   "o plate_b\n"
                   "v 1 -0.5 0\nv 2 -0.5 0\nv 2 0.5 0\nv 1 0.5 0\n"
                   "vt 0.55 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.55 0.95\n"
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

float normal_z(int x) {
	f32_array_t *p = script_mesh_map_pixels(MESH_MAP_WORLD_NORMAL);
	return p->buffer[(32 * RES + x) * 4 + 2] * 2.0 - 1.0;
}

void bake(int maps) {
	script_bake_get_settings()->maps = maps;
	script_bake_selected();
}

void step_build() {
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	bake_settings_t *s = script_bake_get_settings();
	s->res             = RES;
	s->dilation        = 2;
	bake((1 << MESH_MAP_WORLD_NORMAL) | (1 << MESH_MAP_OCCLUSION));
}

void turn_over_a() {
	full = script_bake_texels_baked();
	check(full > 0, string("a full bake computes every texel of the islands (%d)", full));
	object_t *a         = script_get_object("plate_a");
	a->transform->rot.x = 1.0; // 180 degrees around x
	a->transform->rot.w = 0.0;
	transform_build_matrix(a->transform);
	check(script_bake_outdated(MESH_MAP_WORLD_NORMAL), "turning an object over makes the map outdated");
	bake(1 << MESH_MAP_WORLD_NORMAL);
}

void check_partial() {
	int texels = script_bake_texels_baked();
	check(texels > full / 2 - full / 10 && texels < full / 2 + full / 10,
	      string("only the texels of the turned plate are baked again (%d of %d)", texels, full));
	check(normal_z(16) < -0.99 && normal_z(48) > 0.99, string("turned plate faces down, the other one still up (%.2f %.2f)", normal_z(16), normal_z(48)));
	check(!script_bake_outdated(MESH_MAP_WORLD_NORMAL), "the partly baked map is up to date");
	partial_px = script_mesh_map_pixels(MESH_MAP_WORLD_NORMAL);
	script_mesh_map_clear(MESH_MAP_WORLD_NORMAL); // The next bake is a full one
	bake(1 << MESH_MAP_WORLD_NORMAL);
}

void check_same_as_full() {
	check(script_bake_texels_baked() == full, "after a clear the bake is a full one");
	f32_array_t *p    = script_mesh_map_pixels(MESH_MAP_WORLD_NORMAL);
	float        diff = 0.0;
	for (int i = 0; i < RES * RES * 4; ++i) {
		float d = absf(p->buffer[i] - partial_px->buffer[i]);
		diff    = d > diff ? d : diff;
	}
	check(diff < 0.005, string("the partial bake matches a full one, dilation included (largest difference %.4f)", diff));

	// Occlusion casts rays to the other objects: always a full bake
	object_t *b         = script_get_object("plate_b");
	b->transform->rot.x = 1.0;
	b->transform->rot.w = 0.0;
	transform_build_matrix(b->transform);
	bake(1 << MESH_MAP_OCCLUSION);
}

void check_rays_full() {
	check(script_bake_texels_baked() == full, "occlusion is baked again in full");
}

void next_frame() {
	if (step >= 1 && script_bake_busy()) {
		script_notify_on_next_frame(next_frame);
		return;
	}
	if (step == 0) {
		step_build();
	}
	else if (step == 1) {
		turn_over_a();
	}
	else if (step == 2) {
		check_partial();
	}
	else if (step == 3) {
		check_same_as_full();
	}
	else if (step == 4) {
		check_rays_full();
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
