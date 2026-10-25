#include "global.h"

// BAKE-08 and BAKE-09: ids by uv island, curvature from the mesh, and the Opacity and UV Island types of the Bake Texture node.
// Run with: base/tests/paint_headless/run.sh p2_bake_types.c

char *dir_out   = "../smoke";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

int RES = 64;

// A roof and a valley, each made of two planar quads with a uv island per quad: the ridge and the bottom of the
// valley fall on uv seams. Obj v is flipped on import, texel rows run against v.
char *mesh_scene = "o roof\n"
                   "v 0 0 0\nv 1 0 0.5\nv 2 0 0\nv 0 1 0\nv 1 1 0.5\nv 2 1 0\n"
                   "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.45\nvt 0.05 0.45\n"
                   "vt 0.55 0.05\nvt 0.95 0.05\nvt 0.95 0.45\nvt 0.55 0.45\n"
                   "f 1/1 2/2 5/3\nf 1/1 5/3 4/4\n"
                   "f 2/5 3/6 6/7\nf 2/5 6/7 5/8\n"
                   "v 0 2 0.5\nv 1 2 0\nv 2 2 0.5\nv 0 3 0.5\nv 1 3 0\nv 2 3 0.5\n"
                   "vt 0.05 0.55\nvt 0.45 0.55\nvt 0.45 0.95\nvt 0.05 0.95\n"
                   "vt 0.55 0.55\nvt 0.95 0.55\nvt 0.95 0.95\nvt 0.55 0.95\n"
                   "f 7/9 8/10 11/11\nf 7/9 11/11 10/12\n"
                   "f 8/13 9/14 12/15\nf 8/13 12/15 11/16\n";

// Texel columns next to the seam, in the left and right islands, and rows in the roof and in the valley
int COL_LEFT   = 27; // u 0.43
int COL_RIGHT  = 36; // u 0.57
int COL_LEFT2  = 8;  // u 0.13, the other triangle of the left quads
int ROW_ROOF   = 48; // v 0.25
int ROW_VALLEY = 16; // v 0.75

ui_node_t *bake_node = NULL;

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

int same3(int slot, int x0, int y0, int x1, int y1) {
	return absf(px(slot, x0, y0, 0) - px(slot, x1, y1, 0)) < 0.01 && absf(px(slot, x0, y0, 1) - px(slot, x1, y1, 1)) < 0.01 &&
	       absf(px(slot, x0, y0, 2) - px(slot, x1, y1, 2)) < 0.01;
}

char *rgb(int slot, int x, int y) {
	return string("%.2f %.2f %.2f", px(slot, x, y, 0), px(slot, x, y, 1), px(slot, x, y, 2));
}

void bake(int slot) {
	script_bake_get_settings()->maps = 1 << slot;
	script_bake_selected();
}

// Byte color of the texture the bake node made, at a uv point
int node_color(float u, float v) {
	gpu_texture_t *t = script_bake_node_texture(bake_node);
	if (t == NULL) {
		return -1;
	}
	buffer_t *pixels = script_get_texture_pixels(t);
	int       i      = ((int)(v * t->height) * t->width + (int)(u * t->width)) * 4;
	return buffer_get_u8(pixels, i) * 65536 + buffer_get_u8(pixels, i + 1) * 256 + buffer_get_u8(pixels, i + 2);
}

int node_gray(float u, float v) {
	return node_color(u, v) / 65536;
}

void step_build() {
	script_project_new();
	object_t *cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_scene);
	script_object_remove(cube);
	check(script_get_object("roof") != NULL, "test scene appended");

	bake_settings_t *s = script_bake_get_settings();
	check(s->curvature_mode == MESH_MAP_CURVATURE_NORMALS && s->id_source == MESH_MAP_ID_OBJECT, "default curvature and id sources");
	s->res                = RES;
	s->dilation           = 0;
	s->curvature_strength = 4.0;
	s->id_source          = MESH_MAP_ID_UV_ISLAND;
	bake(MESH_MAP_ID);
}

void check_islands() {
	int roof_left    = same3(MESH_MAP_ID, COL_LEFT, ROW_ROOF, COL_LEFT2, ROW_ROOF);
	int valley_right = same3(MESH_MAP_ID, COL_RIGHT, ROW_VALLEY, COL_RIGHT + 16, ROW_VALLEY);
	check(roof_left && valley_right, "one color for the two triangles of an island");
	int a = !same3(MESH_MAP_ID, COL_LEFT, ROW_ROOF, COL_RIGHT, ROW_ROOF);
	int b = !same3(MESH_MAP_ID, COL_LEFT, ROW_ROOF, COL_LEFT, ROW_VALLEY);
	int c = !same3(MESH_MAP_ID, COL_RIGHT, ROW_ROOF, COL_RIGHT, ROW_VALLEY);
	int d = !same3(MESH_MAP_ID, COL_LEFT, ROW_VALLEY, COL_RIGHT, ROW_VALLEY);
	check(a && b && c && d, string("a color per uv island of one object: %s / %s / %s / %s", rgb(MESH_MAP_ID, COL_LEFT, ROW_ROOF),
	                               rgb(MESH_MAP_ID, COL_RIGHT, ROW_ROOF), rgb(MESH_MAP_ID, COL_LEFT, ROW_VALLEY), rgb(MESH_MAP_ID, COL_RIGHT, ROW_VALLEY)));
	check(px(MESH_MAP_ID, 32, ROW_ROOF, 0) == 0.0 && px(MESH_MAP_ID, 32, ROW_ROOF, 1) == 0.0,
	      string("black between the islands: %s", rgb(MESH_MAP_ID, 32, ROW_ROOF)));
	bake(MESH_MAP_CURVATURE);
}

void check_curvature_normals() {
	// The quads are flat and the edges fall on uv seams: the normals of a texel and its neighbours agree everywhere
	float roof   = px(MESH_MAP_CURVATURE, COL_LEFT, ROW_ROOF, 0);
	float valley = px(MESH_MAP_CURVATURE, COL_RIGHT, ROW_VALLEY, 0);
	check(absf(roof - 0.5) < 0.02 && absf(valley - 0.5) < 0.02, string("normals mode misses edges on uv seams: roof %.3f valley %.3f", roof, valley));
	script_bake_get_settings()->curvature_mode = MESH_MAP_CURVATURE_MESH;
	bake(MESH_MAP_CURVATURE);
}

void check_curvature_mesh() {
	float roof_l   = px(MESH_MAP_CURVATURE, COL_LEFT, ROW_ROOF, 0);
	float roof_r   = px(MESH_MAP_CURVATURE, COL_RIGHT, ROW_ROOF, 0);
	float valley_l = px(MESH_MAP_CURVATURE, COL_LEFT, ROW_VALLEY, 0);
	float valley_r = px(MESH_MAP_CURVATURE, COL_RIGHT, ROW_VALLEY, 0);
	check(roof_l > 0.58 && roof_r > 0.58, string("mesh mode: the ridge is convex on both sides of the seam (%.3f %.3f)", roof_l, roof_r));
	check(valley_l < 0.42 && valley_r < 0.42, string("mesh mode: the valley is concave on both sides of the seam (%.3f %.3f)", valley_l, valley_r));

	// Bake Texture node of a material with opacity 0.5, into the paint layer of the project
	script_material_create("bake_types");
	ui_node_t *out = script_material_get_node("OUTPUT_MATERIAL_PBR");
	bake_node      = script_material_create_node_at("TEX_BAKE", -300.0, 0.0);
	check(out != NULL && bake_node != NULL, "material with a bake texture node");
	if (bake_node != NULL) {
		script_material_set_float(out, 1, 1, 0.5); // Opacity
		script_material_set_button(bake_node, 0, BAKE_TYPE_OPACITY);
		script_material_update();
		script_bake_node(bake_node);
	}
}

void check_node_opacity() {
	int g0 = node_gray(0.25, 0.25);
	int g1 = node_gray(0.75, 0.75);
	check(absf(g0 - 128) <= 3 && absf(g1 - 128) <= 3, string("opacity bake writes the opacity, nothing discarded: %d %d", g0, g1));
	script_material_set_button(bake_node, 0, BAKE_TYPE_UV_ISLAND);
	script_get_context()->brush_scale = 2.0; // The islands follow the mesh uvs, not the brush mapping
	script_bake_node(bake_node);
}

void check_node_uv_island() {
	int ll = node_color(0.25, 0.25);
	int lr = node_color(0.75, 0.25);
	int hl = node_color(0.25, 0.75);
	int hr = node_color(0.75, 0.75);
	check(ll != lr && ll != hl && ll != hr && lr != hl && lr != hr && hl != hr, string("uv island bake: a color per island %x %x %x %x", ll, lr, hl, hr));
	check(node_color(0.10, 0.25) == ll && node_color(0.75, 0.30) == lr, "uv island bake: one color inside an island");
	check(ll != 0 && hr != 0, "uv island colors are not black");
	script_get_context()->brush_scale = 1.0;

	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	script_project_new();
	check(script_bake_get_settings()->curvature_mode == MESH_MAP_CURVATURE_NORMALS, "a new project has the default curvature source");
	script_project_open(file_save);
	bake_settings_t *s = script_bake_get_settings();
	check(s->curvature_mode == MESH_MAP_CURVATURE_MESH && s->id_source == MESH_MAP_ID_UV_ISLAND, "curvature and id sources survive save and load");
}

int waits_for_bake(int s) {
	return s >= 1 && s <= 6;
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
		check_islands();
	}
	else if (step == 2) {
		check_curvature_normals();
	}
	else if (step == 3) {
		check_curvature_mesh();
	}
	else if (step == 4) {
		check_node_opacity();
	}
	else if (step == 5) {
		check_node_uv_island();
	}
	else if (step == 6) {
		// project_save() runs on this frame
	}
	else if (step == 7) {
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
	file_save = string("%s/p2_bake_types.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
