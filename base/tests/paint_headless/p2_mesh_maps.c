#include "global.h"

// BAKE-01: mesh map slots baked by the job queue, read by the Mesh Map node, undo, save and load.
// Run with: base/tests/paint_headless/run.sh p2_mesh_maps.c

char *dir_out   = "../smoke";
char *root      = "../../../base/tests/paint_headless";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

int RES = 64;

// A floor meeting a wall, and a thin slab away from them. Obj v is flipped on import:
// floor and wall end up in the top half of the texture (v 0.05-0.45), the slab in the bottom half.
char *mesh_scene = "o mm_floor\n"
                   "v 0 -0.5 0\nv 1 -0.5 0\nv 1 0.5 0\nv 0 0.5 0\n"
                   "vt 0.05 0.55\nvt 0.45 0.55\nvt 0.45 0.95\nvt 0.05 0.95\n"
                   "f 1/1 2/2 3/3\nf 1/1 3/3 4/4\n"
                   "o mm_wall\n"
                   "v 0 -0.5 0\nv 0 0.5 0\nv 0 0.5 1\nv 0 -0.5 1\n"
                   "vt 0.55 0.55\nvt 0.55 0.95\nvt 0.95 0.95\nvt 0.95 0.55\n"
                   "f 5/5 6/6 7/7\nf 5/5 7/7 8/8\n"
                   "o mm_slab_top\n"
                   "v 3 -0.5 0.05\nv 4 -0.5 0.05\nv 4 0.5 0.05\nv 3 0.5 0.05\n"
                   "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.45\nvt 0.05 0.45\n"
                   "f 9/9 10/10 11/11\nf 9/9 11/11 12/12\n"
                   "o mm_slab_bottom\n"
                   "v 3 -0.5 -0.05\nv 3 0.5 -0.05\nv 4 0.5 -0.05\nv 4 -0.5 -0.05\n"
                   "vt 0.55 0.05\nvt 0.55 0.45\nvt 0.95 0.45\nvt 0.95 0.05\n"
                   "f 13/13 14/14 15/15\nf 13/13 15/15 16/16\n";

// Texels of the test, in a RES x RES map
int FLOOR_NEAR = 4;  // Floor next to the wall
int FLOOR_FAR  = 27; // Floor away from the wall
int WALL_LOW   = 36; // Wall at the floor
int WALL_HIGH  = 59; // Top of the wall
int ROW_TOP    = 16; // Floor and wall
int ROW_BOTTOM = 48; // Slab

f32_array_t   *saved_occlusion = NULL;
f32_array_t   *saved_normal    = NULL;
f32_array_t   *saved_id        = NULL;
float          occlusion_near  = 0.0;
gpu_texture_t *before_cancel   = NULL;

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

float px(f32_array_t *p, int x, int y, int c) {
	if (p == NULL) {
		return -1.0;
	}
	return p->buffer[(y * RES + x) * 4 + c];
}

float slot_px(int slot, int x, int y, int c) {
	return px(script_mesh_map_pixels(slot), x, y, c);
}

int near3(f32_array_t *p, int x, int y, float r, float g, float b, float tol) {
	return absf(px(p, x, y, 0) - r) <= tol && absf(px(p, x, y, 1) - g) <= tol && absf(px(p, x, y, 2) - b) <= tol;
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

// Red of a layer texel at a uv point, the base color of the test is gray so the byte order does not matter
float layer_red(slot_layer_t *l, float u, float v) {
	buffer_t *pixels = script_get_texture_pixels(l->texpaint);
	if (pixels == NULL) {
		return -1.0;
	}
	int w = l->texpaint->width;
	int h = l->texpaint->height;
	return buffer_get_u8(pixels, ((int)(v * h) * w + (int)(u * w)) * 4);
}

float texel_u(int x) {
	return (x + 0.5) / RES;
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	check(script_get_object("mm_floor") != NULL && script_get_object("mm_slab_bottom") != NULL, "test scene appended");
	for (int i = 0; i < MESH_MAP_COUNT; ++i) {
		check(script_mesh_map_get(i) == NULL, string("slot %d starts empty", i));
	}
	bake_settings_t *s = script_bake_get_settings();
	check(s->res == 2048 && s->occlusion_samples > 0 && s->dilation > 0, "default bake settings");
	s->res               = RES;
	s->dilation          = 2;
	s->occlusion_samples = 128;
	s->thickness_samples = 64;
}

void step_material() {
	// Occlusion through the Mesh Map node into the base color of a fill layer
	script_material_create("mm_occlusion");
	ui_node_t *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	ui_node_t *n   = script_material_create_node_at("MESH_MAP", -300.0, 0.0);
	check(n != NULL, "Mesh Map node created");
	if (n != NULL) {
		script_material_set_button(n, 0, MESH_MAP_OCCLUSION);
		script_material_connect(n, 0, out, 0);
	}
	script_material_update();
	slot_layer_t *l = script_layer_new_fill();
	script_layer_set_name(l, "mm_fill");
}

void step_bake_all() {
	slot_layer_t *l = layer_by_name("mm_fill");
	check(l != NULL && layer_red(l, texel_u(FLOOR_NEAR), texel_u(ROW_TOP)) > 250, "unbaked occlusion reads white");
	script_bake_mesh_maps();
	check(script_bake_busy(), "bake queued");
}

void check_slots() {
	for (int i = 0; i < MESH_MAP_COUNT; ++i) {
		gpu_texture_t *t = script_mesh_map_get(i);
		check(t != NULL && t->width == RES && t->height == RES, string("slot %d baked at %dx%d", i, RES, RES));
	}

	f32_array_t *op = script_mesh_map_pixels(MESH_MAP_OPACITY);
	check(px(op, FLOOR_NEAR, ROW_TOP, 0) > 0.99 && px(op, WALL_HIGH, ROW_TOP, 0) > 0.99, "opacity is white in the islands");
	check(px(op, 29, ROW_TOP, 0) > 0.99, "dilation fills the texel next to an island");
	check(px(op, 31, ROW_TOP, 0) < 0.01 && px(op, 32, ROW_TOP, 0) < 0.01, "texels past the dilation keep the background");

	f32_array_t *wn = script_mesh_map_pixels(MESH_MAP_WORLD_NORMAL);
	check(near3(wn, FLOOR_FAR, ROW_TOP, 0.5, 0.5, 1.0, 0.02),
	      string("floor world normal %.2f %.2f %.2f", px(wn, FLOOR_FAR, ROW_TOP, 0), px(wn, FLOOR_FAR, ROW_TOP, 1), px(wn, FLOOR_FAR, ROW_TOP, 2)));
	check(near3(wn, WALL_HIGH, ROW_TOP, 1.0, 0.5, 0.5, 0.02),
	      string("wall world normal %.2f %.2f %.2f", px(wn, WALL_HIGH, ROW_TOP, 0), px(wn, WALL_HIGH, ROW_TOP, 1), px(wn, WALL_HIGH, ROW_TOP, 2)));
	check(near3(wn, FLOOR_FAR, ROW_BOTTOM, 0.5, 0.5, 1.0, 0.02) && near3(wn, WALL_HIGH, ROW_BOTTOM, 0.5, 0.5, 0.0, 0.02), "slab faces up and down");

	f32_array_t *nm = script_mesh_map_pixels(MESH_MAP_NORMAL);
	check(near3(nm, FLOOR_FAR, ROW_TOP, 0.5, 0.5, 1.0, 0.01), "tangent normal is flat without a high poly");

	f32_array_t *pos = script_mesh_map_pixels(MESH_MAP_POSITION);
	check(px(pos, FLOOR_FAR, ROW_TOP, 0) > px(pos, FLOOR_NEAR, ROW_TOP, 0) + 0.1, "position x grows along the floor");
	check(px(pos, WALL_HIGH, ROW_TOP, 2) > 0.85 && px(pos, FLOOR_FAR, ROW_TOP, 2) < 0.1,
	      string("position z: wall top %.2f, floor %.2f", px(pos, WALL_HIGH, ROW_TOP, 2), px(pos, FLOOR_FAR, ROW_TOP, 2)));

	f32_array_t *id = script_mesh_map_pixels(MESH_MAP_ID);
	check(near3(id, FLOOR_FAR, ROW_TOP, px(id, FLOOR_NEAR, ROW_TOP, 0), px(id, FLOOR_NEAR, ROW_TOP, 1), px(id, FLOOR_NEAR, ROW_TOP, 2), 0.01),
	      "one id color per object");
	check(!near3(id, WALL_HIGH, ROW_TOP, px(id, FLOOR_FAR, ROW_TOP, 0), px(id, FLOOR_FAR, ROW_TOP, 1), px(id, FLOOR_FAR, ROW_TOP, 2), 0.1),
	      "floor and wall get different id colors");

	f32_array_t *ao   = script_mesh_map_pixels(MESH_MAP_OCCLUSION);
	float        near = px(ao, FLOOR_NEAR, ROW_TOP, 0);
	float        far  = px(ao, FLOOR_FAR, ROW_TOP, 0);
	check(near < far - 0.15 && near < 0.75, string("occlusion next to the wall %.2f, away from it %.2f", near, far));
	check(px(ao, FLOOR_FAR, ROW_BOTTOM, 0) > 0.95, "nothing occludes the top of the slab");
	occlusion_near = near;

	f32_array_t *bent = script_mesh_map_pixels(MESH_MAP_BENT_NORMAL);
	check(px(bent, FLOOR_NEAR, ROW_TOP, 0) > 0.6, string("bent normal leans away from the wall (x %.2f)", px(bent, FLOOR_NEAR, ROW_TOP, 0)));

	f32_array_t *th = script_mesh_map_pixels(MESH_MAP_THICKNESS);
	check(px(th, FLOOR_FAR, ROW_BOTTOM, 0) < 0.5 && px(th, FLOOR_FAR, ROW_TOP, 0) > 0.9,
	      string("thickness: thin slab %.2f, open floor %.2f", px(th, FLOOR_FAR, ROW_BOTTOM, 0), px(th, FLOOR_FAR, ROW_TOP, 0)));

	f32_array_t *cv = script_mesh_map_pixels(MESH_MAP_CURVATURE);
	check(absf(px(cv, FLOOR_FAR, ROW_TOP, 0) - 0.5) < 0.02 && absf(px(cv, WALL_HIGH, ROW_TOP, 0) - 0.5) < 0.02, "flat faces have neutral curvature");

	slot_layer_t *l = layer_by_name("mm_fill");
	float         a = layer_red(l, texel_u(FLOOR_NEAR), texel_u(ROW_TOP));
	float         b = layer_red(l, texel_u(FLOOR_FAR), texel_u(ROW_TOP));
	check(a < b - 20.0, string("fill layer reads the baked occlusion: %.0f next to the wall, %.0f away", a, b));

	saved_occlusion = ao;
}

void step_rebake() {
	script_bake_get_settings()->occlusion_distance = 0.02;
	script_bake_mesh_map(MESH_MAP_OCCLUSION);
}

void check_rebake() {
	float near = slot_px(MESH_MAP_OCCLUSION, FLOOR_NEAR, ROW_TOP, 0);
	check(near > occlusion_near + 0.1, string("shorter occlusion distance: %.2f (was %.2f)", near, occlusion_near));
	script_undo();
	check(absf(slot_px(MESH_MAP_OCCLUSION, FLOOR_NEAR, ROW_TOP, 0) - occlusion_near) < 0.005, "undo brings the previous bake back");
	script_redo();
	check(absf(slot_px(MESH_MAP_OCCLUSION, FLOOR_NEAR, ROW_TOP, 0) - near) < 0.005, "redo brings the new bake back");

	saved_id = script_mesh_map_pixels(MESH_MAP_ID);
	script_mesh_map_clear(MESH_MAP_ID);
	check(script_mesh_map_get(MESH_MAP_ID) == NULL, "clear empties the slot");
	script_undo();
	check(script_mesh_map_get(MESH_MAP_ID) != NULL && near3(script_mesh_map_pixels(MESH_MAP_ID), FLOOR_FAR, ROW_TOP, px(saved_id, FLOOR_FAR, ROW_TOP, 0),
	                                                        px(saved_id, FLOOR_FAR, ROW_TOP, 1), px(saved_id, FLOOR_FAR, ROW_TOP, 2), 0.005),
	      "undo of clear restores the slot");
}

void step_cancel() {
	before_cancel                                 = script_mesh_map_get(MESH_MAP_THICKNESS);
	script_bake_get_settings()->res               = 256;
	script_bake_get_settings()->thickness_samples = 512;
	script_bake_mesh_map(MESH_MAP_THICKNESS);
}

void check_cancel() {
	check(script_bake_busy(), "long bake running");
	script_bake_cancel();
	check(!script_bake_busy(), "cancel stops the queue");
	check(script_mesh_map_get(MESH_MAP_THICKNESS) == before_cancel, "a cancelled bake leaves the slot as it was");
	script_bake_get_settings()->res               = RES;
	script_bake_get_settings()->thickness_samples = 64;
}

void step_save() {
	saved_occlusion = script_mesh_map_pixels(MESH_MAP_OCCLUSION);
	saved_normal    = script_mesh_map_pixels(MESH_MAP_WORLD_NORMAL);
	saved_id        = script_mesh_map_pixels(MESH_MAP_ID);
	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

int same(f32_array_t *a, f32_array_t *b, float tol) {
	if (a == NULL || b == NULL || a->length != b->length) {
		return 0;
	}
	for (int i = 0; i < a->length; ++i) {
		if (absf(a->buffer[i] - b->buffer[i]) > tol) {
			return 0;
		}
	}
	return 1;
}

void step_reopen() {
	check(iron_file_exists(file_save), "project saved");
	script_project_new();
	check(script_mesh_map_get(MESH_MAP_OCCLUSION) == NULL, "a new project has no mesh maps");
	check(script_bake_get_settings()->res == 2048, "a new project has the default bake settings");
	script_project_open(file_save);
	for (int i = 0; i < MESH_MAP_COUNT; ++i) {
		check(script_mesh_map_get(i) != NULL, string("slot %d survived save and load", i));
	}
	check(same(saved_occlusion, script_mesh_map_pixels(MESH_MAP_OCCLUSION), 0.001), "occlusion pixels survived save and load");
	check(same(saved_normal, script_mesh_map_pixels(MESH_MAP_WORLD_NORMAL), 0.001), "16 bit world normal pixels survived save and load");
	check(same(saved_id, script_mesh_map_pixels(MESH_MAP_ID), 0.001), "id pixels survived save and load");
	check(script_bake_get_settings()->res == RES && absf(script_bake_get_settings()->occlusion_distance - 0.02) < 0.001,
	      "bake settings survived save and load");
}

void step_old_project() {
	script_project_open(string("%s/fixtures/p1_v22_project.arm", root));
	check(script_layer_count() > 0, "a version 22 project opens");
	check(script_mesh_map_get(MESH_MAP_OCCLUSION) == NULL && script_bake_get_settings()->res == 2048, "a version 22 project has no mesh maps");
}

// Steps that wait for the bake queue repeat until it is empty
int waits_for_bake(int s) {
	return s == 3 || s == 5 || s == 8;
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
		step_material();
	}
	else if (step == 2) {
		step_bake_all();
	}
	else if (step == 3) {
		check_slots();
	}
	else if (step == 4) {
		step_rebake();
	}
	else if (step == 5) {
		check_rebake();
	}
	else if (step == 6) {
		step_cancel();
	}
	else if (step == 7) {
		check_cancel();
	}
	else if (step == 8) {
		step_save();
	}
	else if (step == 9) {
		// project_save() runs on this frame
	}
	else if (step == 10) {
		step_reopen();
	}
	else if (step == 11) {
		step_old_project();
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
	file_save = string("%s/p2_mesh_maps.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
