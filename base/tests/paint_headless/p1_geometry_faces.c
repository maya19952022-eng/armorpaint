#include "global.h"

// LAY-11: faces of the geometry mask. geo_faces is a strip of four quads on the plane y = 0, quads 0 and 1 in
// uv island A, quads 2 and 3 in island B (same positions as quad 1 at x = 0, other uvs), and a disconnected
// quad, part 2. geo_other is one quad. Every uv island is a vertical band over v 0.1..0.9, the exported base
// color of a red layer over the gray base is read over the band of each quad. Faces are picked with rays from
// world space points like the select tool: triangle, polygon, connected part, uv island and object, added
// and removed, undo and redo, include and exclude, with objects, tiles and groups, effects, the viewport,
// painting, layer operations, save and reopen, renames, a mesh change and a project saved before faces.
// Run with: base/tests/paint_headless/run.sh p1_geometry_faces.c

char *root      = "../../../base/tests/paint_headless";
char *dir_out   = "../smoke";
char *file_arm  = NULL;
char *dir_exp   = NULL;
float tolerance = 3.0;

int step         = 0;
int wait         = 0;
int checks       = 0;
int failures     = 0;
int export_count = 0;

object_t        *cube      = NULL;
slot_material_t *material0 = NULL; // Default material, adding an effect selects the material of the effect

// Strip quad k spans x -1 + 0.5k .. -0.5 + 0.5k and z -0.5..0.5, triangles 2k = (a, b, c) and 2k + 1 = (a, c, d)
char *mesh_faces = "o geo_faces\n"
                   "v -1.0 0.0 -0.5\nv -0.5 0.0 -0.5\nv 0.0 0.0 -0.5\nv 0.5 0.0 -0.5\nv 1.0 0.0 -0.5\n"
                   "v -1.0 0.0 0.5\nv -0.5 0.0 0.5\nv 0.0 0.0 0.5\nv 0.5 0.0 0.5\nv 1.0 0.0 0.5\n"
                   "v -1.0 0.0 0.6\nv -0.2 0.0 0.6\nv -0.2 0.0 0.9\nv -1.0 0.0 0.9\n"
                   "vt 0.02 0.1\nvt 0.12 0.1\nvt 0.22 0.1\nvt 0.02 0.9\nvt 0.12 0.9\nvt 0.22 0.9\n"
                   "vt 0.27 0.1\nvt 0.37 0.1\nvt 0.47 0.1\nvt 0.27 0.9\nvt 0.37 0.9\nvt 0.47 0.9\n"
                   "vt 0.52 0.1\nvt 0.72 0.1\nvt 0.72 0.9\nvt 0.52 0.9\n"
                   "f 1/1 2/2 7/5\nf 1/1 7/5 6/4\n"
                   "f 2/2 3/3 8/6\nf 2/2 8/6 7/5\n"
                   "f 3/7 4/8 9/11\nf 3/7 9/11 8/10\n"
                   "f 4/8 5/9 10/12\nf 4/8 10/12 9/11\n"
                   "f 11/13 12/14 13/15\nf 11/13 13/15 14/16\n"
                   "o geo_other\n"
                   "v 0.2 0.0 0.6\nv 1.0 0.0 0.6\nv 1.0 0.0 0.9\nv 0.2 0.0 0.9\n"
                   "vt 0.77 0.1\nvt 0.97 0.1\nvt 0.97 0.9\nvt 0.77 0.9\n"
                   "f 15/17 16/18 17/19\nf 15/17 17/19 18/20\n";

// Exported bands, inside of the islands: quads 0 to 3, part 2, geo_other
float band_u0[6] = {0.03, 0.13, 0.28, 0.38, 0.53, 0.78};
float band_u1[6] = {0.11, 0.21, 0.36, 0.46, 0.71, 0.96};
float frac_red[6];
float frac_gray[6];
float band_mean[18];
float saved_mean[18];

// Triangles picked at the centroids C(k, 0) and C(k, 1) of the strip, part 2 and geo_other
int   tri[8];
int   tri_part2 = -1;
int   tri_other = -1;
float part2_x   = -0.4666667;
float other_x   = 0.7333333;
float upper_z   = 0.7;

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

slot_layer_t *layer_by_name(char *name) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (string_equals(l->name, name)) {
			return l;
		}
	}
	return NULL;
}

object_t *geo() {
	return script_get_object("geo_faces");
}

object_t *other() {
	return script_get_object("geo_other");
}

slot_layer_t *red() {
	return layer_by_name("faces_red");
}

// Centroid of triangle s of strip quad k
float cx(int k, int s) {
	float x0 = -1.0 + 0.5 * k;
	float x1 = x0 + 0.5;
	return s == 0 ? (x0 + 2.0 * x1) / 3.0 : (2.0 * x0 + x1) / 3.0;
}

float cz(int s) {
	return s == 0 ? -1.0 / 6.0 : 1.0 / 6.0;
}

int pick(int k, int s) {
	return script_pick_face_world(geo(), cx(k, s), 0.0, cz(s));
}

int select_at(slot_layer_t *l, int k, int s, int mode, int selected) {
	return script_layer_select_faces_world(l, cx(k, s), 0.0, cz(s), mode, selected);
}

int count_geo(slot_layer_t *l) {
	return script_layer_get_geometry_face_count(l, geo());
}

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

int is_red(float r, float g) {
	return r > 240.0 && g < 15.0;
}

int is_gray(float r, float g) {
	return absf(r - 128.0) <= tolerance && absf(g - 128.0) <= tolerance;
}

// Red and gray fractions and the mean color of every band of the exported base color, over v 0.2..0.8
void measure(buffer_t *pixels, int w, int h) {
	for (int b = 0; b < 6; ++b) {
		int   red    = 0;
		int   gray   = 0;
		int   n      = 0;
		float sum[3] = {0.0, 0.0, 0.0};
		for (int i = 0; i < 20; ++i) {
			float u = band_u0[b] + (band_u1[b] - band_u0[b]) * i / 19.0;
			for (int j = 0; j < 20; ++j) {
				float v  = 0.2 + 0.6 * j / 19.0;
				int   p  = ((int)(v * h) * w + (int)(u * w)) * 4;
				float pr = buffer_get_u8(pixels, p);
				float pg = buffer_get_u8(pixels, p + 1);
				float pb = buffer_get_u8(pixels, p + 2);
				if (is_red(pr, pg)) {
					red += 1;
				}
				else if (is_gray(pr, pg)) {
					gray += 1;
				}
				sum[0] += pr;
				sum[1] += pg;
				sum[2] += pb;
				n += 1;
			}
		}
		frac_red[b]          = red / (float)n;
		frac_gray[b]         = gray / (float)n;
		band_mean[b * 3]     = sum[0] / n;
		band_mean[b * 3 + 1] = sum[1] / n;
		band_mean[b * 3 + 2] = sum[2] / n;
	}
}

void export_bands() {
	for (int b = 0; b < 6; ++b) {
		frac_red[b]  = -1.0;
		frac_gray[b] = -1.0;
	}
	export_count += 1;
	char *dir = string("%s/p1_faces%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	any_array_t *files = file_read_directory(dir);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, "_base.png")) {
			continue;
		}
		script_import_asset(string("%s/%s", dir, name), false);
		gpu_texture_t *tex    = script_get_texture(name);
		buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
		if (pixels != NULL) {
			measure(pixels, tex->width, tex->height);
		}
		script_texture_delete(name);
	}
}

char *bands() {
	return string("red %.2f %.2f %.2f %.2f %.2f %.2f, gray %.2f %.2f %.2f %.2f %.2f %.2f", frac_red[0], frac_red[1], frac_red[2], frac_red[3], frac_red[4],
	              frac_red[5], frac_gray[0], frac_gray[1], frac_gray[2], frac_gray[3], frac_gray[4], frac_gray[5]);
}

int red_band(int b) {
	return frac_red[b] > 0.95;
}

int gray_band(int b) {
	return frac_gray[b] > 0.95;
}

int half_band(int b) {
	return frac_red[b] >= 0.35 && frac_red[b] <= 0.65;
}

// Every band red (1), gray (0), half red (2) or not checked (-1)
void expect_bands(char *when, int q0, int q1, int q2, int q3, int p2, int ot) {
	export_bands();
	int want[6] = {q0, q1, q2, q3, p2, ot};
	int ok      = 1;
	for (int b = 0; b < 6; ++b) {
		if (want[b] == 1 && !red_band(b)) {
			ok = 0;
		}
		else if (want[b] == 0 && !gray_band(b)) {
			ok = 0;
		}
		else if (want[b] == 2 && !half_band(b)) {
			ok = 0;
		}
	}
	check(ok, string("%s: %s", when, bands()));
}

void save_bands() {
	for (int i = 0; i < 18; ++i) {
		saved_mean[i] = band_mean[i];
	}
}

int same_bands() {
	for (int i = 0; i < 18; ++i) {
		if (absf(band_mean[i] - saved_mean[i]) > tolerance) {
			printf("[smoke]        band %d channel %d: %.1f, was %.1f", i / 3, i % 3, band_mean[i], saved_mean[i]);
			return 0;
		}
	}
	return 1;
}

// Alpha of a layer texture at a uv point, render targets can be BGRA but alpha stays last
float alpha_at(slot_layer_t *l, float u, float v) {
	buffer_t *pixels = script_get_texture_pixels(l->texpaint);
	if (pixels == NULL) {
		return -1.0;
	}
	int w = l->texpaint->width;
	int h = l->texpaint->height;
	return buffer_get_u8(pixels, ((int)(v * h) * w + (int)(u * w)) * 4 + 3);
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	cube = script_get_context()->paint_object->base;
	script_append_mesh_obj(mesh_faces);
}

void step_remove_cube() {
	script_object_remove(cube);
}

// 1, 3
void step_layers() {
	check(geo() != NULL && other() != NULL, "geo_faces and geo_other appended");
	int a = script_object_triangle_count(geo());
	int b = script_object_triangle_count(other());
	check(a == 10 && b == 2, string("triangle counts %d and %d", a, b));
	material0       = script_get_context()->material;
	slot_layer_t *l = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	script_layer_set_name(l, "faces_red");
	check(count_geo(l) == 0 && script_layer_get_geometry_face_count(l, other()) == 0 && script_layer_geometry_applies(l, geo()) &&
	          script_layer_geometry_applies(l, other()) && script_layer_geometry_face_applies(l, geo(), 0),
	      "a new layer has no faces and applies everywhere");
}

// 2
void step_pick() {
	int distinct = 1;
	for (int k = 0; k < 4; ++k) {
		for (int s = 0; s < 2; ++s) {
			tri[k * 2 + s] = pick(k, s);
			if (tri[k * 2 + s] < 0) {
				distinct = 0;
			}
			for (int j = 0; j < k * 2 + s; ++j) {
				if (tri[j] == tri[k * 2 + s]) {
					distinct = 0;
				}
			}
		}
	}
	check(distinct, string("the strip centroids pick 8 triangles: %d %d %d %d %d %d %d %d", tri[0], tri[1], tri[2], tri[3], tri[4], tri[5], tri[6], tri[7]));
	check(script_pick_face_world(geo(), 3.0, 0.0, 3.0) == -1, "a point off the mesh picks nothing");
	tri_part2   = script_pick_face_world(geo(), part2_x, 0.0, upper_z);
	int outside = tri_part2 >= 0;
	for (int i = 0; i < 8; ++i) {
		if (tri[i] == tri_part2) {
			outside = 0;
		}
	}
	check(outside, string("part 2 picks another triangle (%d)", tri_part2));
	tri_other = script_pick_face_world(other(), other_x, 0.0, upper_z);
	check(tri_other >= 0 && script_pick_face_world(geo(), other_x, 0.0, upper_z) == -1, "geo_other is picked as itself");
}

// 4, 5
void step_triangle() {
	slot_layer_t *l = red();
	int           t = select_at(l, 0, 0, GEOMETRY_SELECT_TRIANGLE, 1);
	check(t == tri[0] && script_layer_get_geometry_face(l, geo(), t) && count_geo(l) == 1 && !script_layer_get_geometry_face(l, geo(), tri[1]),
	      string("triangle at C(0,0) selects triangle %d only (count %d)", t, count_geo(l)));
	script_undo();
	check(count_geo(l) == 0, "undo removes it");
	script_redo();
	check(count_geo(l) == 1 && script_layer_get_geometry_face(l, geo(), tri[0]), "redo selects it again in one step");
}

// 6, 7, 8
void step_polygon() {
	slot_layer_t *l = red();
	select_at(l, 1, 0, GEOMETRY_SELECT_POLYGON, 1);
	check(count_geo(l) == 3 && script_layer_get_geometry_face(l, geo(), tri[2]) && script_layer_get_geometry_face(l, geo(), tri[3]),
	      string("polygon at C(1,0) selects both triangles of quad 1 (count %d)", count_geo(l)));
	expect_bands("include: quad 1 red, half of quad 0", 2, 1, 0, 0, 0, 0);
	check(script_layer_geometry_face_applies(l, geo(), tri[2]) && !script_layer_geometry_face_applies(l, geo(), tri[4]),
	      "the layer applies on quad 1, not on quad 2");
	check(script_layer_geometry_applies(l, geo()) && !script_layer_geometry_applies(l, other()), "the layer applies on geo_faces only");
}

// 9
void step_exclude() {
	slot_layer_t *l = red();
	script_layer_set_geometry_mode(l, GEOMETRY_MASK_EXCLUDE);
	expect_bands("exclude: quad 1 gray, the rest red, half of quad 0", 2, 0, 1, 1, 1, 1);
	check(!script_layer_geometry_face_applies(l, geo(), tri[2]) && script_layer_geometry_face_applies(l, geo(), tri[4]), "exclude flips the faces");
	script_undo();
	check(script_layer_get_geometry_mode(l) == GEOMETRY_MASK_INCLUDE && count_geo(l) == 3, "undo goes back to include");
}

// 10
void step_remove() {
	slot_layer_t *l = red();
	select_at(l, 0, 0, GEOMETRY_SELECT_TRIANGLE, 0);
	check(count_geo(l) == 2 && !script_layer_get_geometry_face(l, geo(), tri[0]), string("remove at C(0,0) (count %d)", count_geo(l)));
	script_undo();
	check(count_geo(l) == 3, "undo of the removal");
}

// 11, 12, 13
void step_expand() {
	slot_layer_t *l = red();
	script_layer_clear_geometry_faces(l);
	check(count_geo(l) == 0, "clear faces");
	select_at(l, 2, 0, GEOMETRY_SELECT_CONNECTED, 1);
	check(count_geo(l) == 8 && !script_layer_get_geometry_face(l, geo(), tri_part2),
	      string("connected at C(2,0) selects the strip without part 2 (count %d)", count_geo(l)));

	script_layer_clear_geometry_faces(l);
	select_at(l, 2, 0, GEOMETRY_SELECT_UV_ISLAND, 1);
	check(count_geo(l) == 4 && script_layer_get_geometry_face(l, geo(), tri[7]) && !script_layer_get_geometry_face(l, geo(), tri[2]),
	      string("uv island at C(2,0) selects quads 2 and 3 (count %d)", count_geo(l)));

	script_layer_clear_geometry_faces(l);
	script_layer_select_faces_world(l, part2_x, 0.0, upper_z, GEOMETRY_SELECT_OBJECT, 1);
	check(count_geo(l) == 10, string("object at part 2 selects every face (count %d)", count_geo(l)));
}

// 14, 15, 16
void step_objects() {
	slot_layer_t *l = red();
	script_layer_set_geometry_object(l, other(), true);
	script_layer_clear_geometry_faces(l);
	check(count_geo(l) == 0 && script_layer_get_geometry_object(l, other()), "clear faces keeps the checked geo_other");
	script_undo();
	check(count_geo(l) == 10 && script_layer_get_geometry_object(l, other()), "undo of clear faces");

	script_layer_clear_geometry_faces(l);
	select_at(l, 1, 0, GEOMETRY_SELECT_POLYGON, 1);
	expect_bands("geo_other checked and quad 1 faces add up", 0, 1, 0, 0, 0, 1);

	script_layer_select_faces_world(l, other_x, 0.0, upper_z, GEOMETRY_SELECT_TRIANGLE, 0);
	check(!script_layer_get_geometry_object(l, other()) && script_layer_get_geometry_face_count(l, other()) == 1,
	      "removing a face of the checked geo_other moves it to its faces");
	expect_bands("geo_other keeps one of its triangles", 0, 1, 0, 0, 0, 2);
}

// 17
void step_remove_empty() {
	slot_layer_t *l = red();
	script_layer_clear_geometry_mask(l);
	select_at(l, 3, 0, GEOMETRY_SELECT_POLYGON, 0);
	expect_bands("removing quad 3 from an empty mask keeps the rest", 1, 1, 1, 0, 1, 1);
}

// 18
void step_drag() {
	slot_layer_t *l = red();
	script_layer_clear_geometry_mask(l);
	int changed = script_layer_select_faces_drag_world(l, -0.95, 0.0, -1.0 / 6.0, 0.95, 0.0, -1.0 / 6.0, GEOMETRY_SELECT_TRIANGLE, 1);
	check(changed == 8 && count_geo(l) == 8, string("a drag across the strip selects 8 triangles (%d changed, count %d)", changed, count_geo(l)));
	script_undo();
	check(count_geo(l) == 0, "one undo for the whole drag");
}

// 19
void step_tiles() {
	slot_layer_t *l = red();
	select_at(l, 1, 0, GEOMETRY_SELECT_POLYGON, 1);
	script_layer_set_geometry_tile(l, 1002, true);
	expect_bands("tile 1002 restricts the faces to nothing", 0, 0, 0, 0, 0, 0);
	script_undo();
	expect_bands("undo of the tile", 0, 1, 0, 0, 0, 0);
}

// 20
void step_group() {
	slot_layer_t *l = red();
	script_layer_clear_geometry_mask(l);
	slot_layer_t *group = script_layer_new_group(l);
	check(group != NULL, "group");
	script_layer_set_name(group, "faces_group");
	select_at(group, 3, 0, GEOMETRY_SELECT_POLYGON, 1);
	check(script_layer_get_geometry_face_count(group, geo()) == 2 && count_geo(l) == 0, "the group has the faces");
	expect_bands("the group includes quad 3", 0, 0, 0, 1, 0, 0);
	select_at(l, 3, 0, GEOMETRY_SELECT_POLYGON, 1);
	script_layer_set_geometry_mode(l, GEOMETRY_MASK_EXCLUDE);
	expect_bands("the layer excludes the quad 3 its group includes", 0, 0, 0, 0, 0, 0);
	script_undo();
	script_undo();
	expect_bands("two undos", 0, 0, 0, 1, 0, 0);
	script_layer_clear_geometry_mask(group);
	script_layer_set_group(l, NULL);
	check(l->parent == NULL, "layer out of the group");
}

// 21
slot_layer_t *effect = NULL;

void step_effect() {
	slot_layer_t *l = red();
	select_at(l, 1, 0, GEOMETRY_SELECT_POLYGON, 1);
	effect = script_layer_add_effect(l, EFFECT_TYPE_LEVELS);
	check(effect != NULL, "levels effect");
}

void step_effect_check() {
	expect_bands("the effect follows the faces of its layer", 0, -1, 0, 0, 0, 0);
	check(!gray_band(1), "the effect shows on quad 1");
	script_layer_delete(effect);
	check(layer_by_name("faces_red") != NULL && red()->parent == NULL, "effect deleted");
}

// 22, red and gray pixels on the middle row of a screenshot of the viewport. Returns 1 once counted, 0 while
// waiting for the file.
int sub        = 0; // Frame counter of a step that repeats
int step_again = 0; // Set by a step to run again on the next frame
int red_count  = 0;
int red_min    = 0; // Leftmost and rightmost red pixel
int red_max    = 0;
int gray_count = 0;
int gray_min   = 0;
int gray_max   = 0;

int viewport_row(char *name) {
	char *path = string("%s/%s.png", dir_out, name);
	if (sub == 0) {
		if (iron_file_exists(path)) {
			iron_delete_file(path);
		}
		script_screenshot(path);
	}
	if (!iron_file_exists(path) && sub < 60) {
		return 0;
	}
	red_count  = 0;
	gray_count = 0;
	script_import_asset(path, false);
	gpu_texture_t *tex    = script_get_texture(string("%s.png", name));
	buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
	if (pixels != NULL) {
		red_min  = tex->width;
		gray_min = tex->width;
		red_max  = -1;
		gray_max = -1;
		int y    = tex->height / 2;
		for (int x = 0; x < tex->width; ++x) {
			int p = (y * tex->width + x) * 4;
			int r = buffer_get_u8(pixels, p);
			int g = buffer_get_u8(pixels, p + 1);
			int b = buffer_get_u8(pixels, p + 2);
			if (r > 150 && g < 60 && b < 60) {
				red_count += 1;
				red_min = x < red_min ? x : red_min;
				red_max = x > red_max ? x : red_max;
			}
			else if (r > 100 && absf(r - g) < 20 && absf(r - b) < 20) { // Gray base color of the quads
				gray_count += 1;
				gray_min = x < gray_min ? x : gray_min;
				gray_max = x > gray_max ? x : gray_max;
			}
		}
	}
	script_texture_delete(string("%s.png", name));
	printf("[smoke]        viewport %s: red %d (x %d..%d), gray %d (x %d..%d)", name, red_count, red_min, red_max, gray_count, gray_min, gray_max);
	return 1;
}

void step_viewport_include() {
	if (sub == 0) {
		slot_layer_t *l = red();
		script_layer_clear_geometry_mask(l);
		select_at(l, 0, 0, GEOMETRY_SELECT_POLYGON, 1);
		select_at(l, 1, 0, GEOMETRY_SELECT_POLYGON, 1);
		context_set_viewport_mode(1); // VIEWPORT_MODE_BASE_COLOR
	}
	if (!viewport_row("p1_faces_include")) {
		step_again = 1;
		return;
	}
	check(count_geo(red()) == 4 && red_count > 10 && gray_count > 10 && red_max < gray_min, "viewport: faces of quads 0 and 1 show red on the left half only");
}

void step_viewport_exclude() {
	if (sub == 0) {
		script_layer_set_geometry_mode(red(), GEOMETRY_MASK_EXCLUDE);
	}
	if (!viewport_row("p1_faces_exclude")) {
		step_again = 1;
		return;
	}
	check(red_count > 10 && gray_count > 10 && gray_max < red_min, "viewport: excluded faces show red on the right half only");
	script_layer_set_geometry_mode(red(), GEOMETRY_MASK_INCLUDE);
}

// The select tool with the Faces target draws the selection over the viewport
void step_select_tool() {
	if (sub == 0) {
		context_select_tool(TOOL_TYPE_SELECT);
		script_get_context()->select_faces = true;
		script_get_context()->ddirty       = 2;
	}
	if (sub < 3) {
		step_again = 1;
		return;
	}
	check(script_get_context()->tool == TOOL_TYPE_SELECT && script_get_context()->select_faces, "select tool with the Faces target");
	script_get_context()->select_faces = false;
	context_select_tool(TOOL_TYPE_BRUSH);
	context_set_viewport_mode(0); // VIEWPORT_MODE_LIT
}

// 23
void step_paint() {
	slot_layer_t *paint = script_layer_new();
	script_layer_set_name(paint, "faces_paint");
	select_at(paint, 2, 0, GEOMETRY_SELECT_POLYGON, 1);
	check(alpha_at(paint, 0.32, 0.5) < 1.0 && alpha_at(paint, 0.17, 0.5) < 1.0, "the paint layer starts empty");
	script_layer_set(paint);
	script_material_set(material0);
	context_select_tool(TOOL_TYPE_FILL);
	script_paint(0.5, 0.5);
	script_paint_end();
	float in  = alpha_at(paint, 0.32, 0.5);
	float out = alpha_at(paint, 0.17, 0.5);
	check(in > 250.0 && out < 1.0, string("filling paints quad 2 only (alpha %.0f on quad 2, %.0f on quad 1)", in, out));
	script_undo();
	check(alpha_at(paint, 0.32, 0.5) < 1.0, "undo of the fill");
	script_layer_clear_geometry_faces(paint);
	script_paint(0.5, 0.5);
	script_paint_end();
	check(alpha_at(paint, 0.32, 0.5) > 250.0 && alpha_at(paint, 0.17, 0.5) > 250.0, "without the faces the fill paints both");
	context_select_tool(TOOL_TYPE_BRUSH);
	script_layer_delete(paint);
	check(layer_by_name("faces_paint") == NULL, "paint layer deleted");
}

// The same faces on three triangles
int same_faces(slot_layer_t *a, slot_layer_t *b) {
	int ids[3] = {tri[0], tri[2], tri[4]};
	for (int i = 0; i < 3; ++i) {
		if (script_layer_get_geometry_face(a, geo(), ids[i]) != script_layer_get_geometry_face(b, geo(), ids[i])) {
			return 0;
		}
	}
	return count_geo(a) == count_geo(b);
}

// 24, 25
void step_duplicate() {
	slot_layer_t *l = red();
	script_layer_clear_geometry_mask(l);
	select_at(l, 0, 0, GEOMETRY_SELECT_TRIANGLE, 1);
	select_at(l, 1, 0, GEOMETRY_SELECT_POLYGON, 1);
	int           layers = script_layer_count();
	slot_layer_t *dup    = script_layer_duplicate(l);
	check(dup != NULL && dup != l && count_geo(dup) == 3 && same_faces(dup, l), "duplicate keeps the faces");
	script_undo();
	check(script_layer_count() == layers && count_geo(red()) == 3 && script_layer_get_geometry_face(red(), geo(), tri[0]),
	      "undo removes the copy, the source keeps its faces");

	script_layer_paste(red(), red());
	slot_layer_t *copy = script_get_context()->layer;
	check(copy != red() && script_layer_count() == layers + 1 && count_geo(copy) == 3 && same_faces(copy, red()), "paste keeps the faces");
	script_undo();
	check(script_layer_count() == layers && count_geo(red()) == 3, "undo of the paste");
}

// 26
void step_delete() {
	export_bands();
	save_bands();
	script_layer_delete(red());
	check(red() == NULL, "layer deleted");
	script_undo();
	check(red() != NULL && count_geo(red()) == 3 && script_layer_get_geometry_face(red(), geo(), tri[0]), "undo of the delete brings the faces back");
	export_bands();
	check(same_bands(), string("same export after undo of the delete: %s", bands()));
}

// 28
void step_flatten() {
	slot_layer_t *l     = red();
	slot_layer_t *group = script_layer_new_group(l); // The group of step_group went away once empty
	check(group != NULL && l->parent == group, "layer in a group");
	script_layer_set_name(group, "faces_flat_group");
	select_at(group, 1, 0, GEOMETRY_SELECT_POLYGON, 1);
	expect_bands("group with quad 1, layer with quad 1 and half of quad 0", 0, 1, 0, 0, 0, 0);
	save_bands();
	slot_layer_t *flat = script_layer_flatten_group(group);
	check(flat != NULL && count_geo(flat) == 0 && script_layer_geometry_applies(flat, other()), "flatten bakes the faces");
	export_bands();
	check(same_bands(), string("same export after the flatten: %s", bands()));
	script_undo();
	check(count_geo(red()) == 3, "undo of the flatten");
	export_bands();
	check(same_bands(), "same export after undo of the flatten");
	script_layer_set_group(red(), NULL);
}

// 27
void step_merge() {
	slot_layer_t *l = red();
	check(l->parent == NULL && count_geo(l) == 3, "layer out of the group");
	expect_bands("before merge down", 2, 1, 0, 0, 0, 0);
	save_bands();
	slot_layer_t *merged = script_layer_merge_down(l);
	check(merged != NULL && red() == NULL && count_geo(merged) == 0 && script_layer_geometry_applies(merged, other()), "merge down leaves no faces");
	export_bands();
	check(same_bands(), string("same export after merge down: %s", bands()));
	script_undo();
	check(red() != NULL && count_geo(red()) == 3, "undo of the merge brings the faces back");
}

// 29
void step_save() {
	slot_layer_t *l = red();
	script_layer_set_geometry_object(l, other(), true);
	slot_layer_t *keep = script_layer_new_color(0xff2050c0, 1.0, 0.5, 0.0);
	script_layer_set_name(keep, "faces_keep");
	select_at(keep, 3, 0, GEOMETRY_SELECT_POLYGON, 1);
	script_layer_select_faces_world(keep, other_x, 0.0, upper_z, GEOMETRY_SELECT_TRIANGLE, 1);
	script_layer_set_geometry_mode(keep, GEOMETRY_MASK_EXCLUDE);
	export_bands();
	save_bands();
	if (iron_file_exists(file_arm)) {
		iron_delete_file(file_arm);
	}
	project_filepath_set(file_arm);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	check(iron_file_exists(file_arm), "project saved");
	script_project_new();
	script_project_open(file_arm);
	slot_layer_t *l    = red();
	slot_layer_t *keep = layer_by_name("faces_keep");
	check(l != NULL && keep != NULL && geo() != NULL && other() != NULL, "reopened");
	if (l == NULL || keep == NULL || geo() == NULL || other() == NULL) {
		return;
	}
	check(count_geo(l) == 3 && script_layer_get_geometry_face_count(l, other()) == 0 && count_geo(keep) == 2 &&
	          script_layer_get_geometry_face_count(keep, other()) == 1,
	      "face counts per object");
	check(script_layer_get_geometry_face(l, geo(), tri[0]) && !script_layer_get_geometry_face(l, geo(), tri[1]) &&
	          script_layer_get_geometry_face(l, geo(), tri[3]) && script_layer_get_geometry_face(keep, geo(), tri[6]) &&
	          script_layer_get_geometry_face(keep, other(), tri_other),
	      "face bits");
	check(script_layer_get_geometry_mode(l) == GEOMETRY_MASK_INCLUDE && script_layer_get_geometry_mode(keep) == GEOMETRY_MASK_EXCLUDE &&
	          script_layer_get_geometry_object(l, other()) && !script_layer_get_geometry_object(keep, other()),
	      "modes and objects");
	export_bands();
	check(same_bands(), string("same export after reopening: %s", bands()));
	script_layer_delete(keep);
}

// 30
void step_rename() {
	slot_layer_t *l = red();
	script_object_set_name(geo(), "geo_renamed");
	object_t *o = script_get_object("geo_renamed");
	check(o != NULL && script_get_object("geo_faces") == NULL && script_layer_get_geometry_face_count(l, o) == 3 &&
	          script_layer_get_geometry_face(l, o, tri[0]),
	      "the faces follow the renamed object");
	script_layer_set_geometry_object(l, other(), false);
	expect_bands("before the mesh change", 2, 1, 0, 0, 0, 0);
}

// 32
void step_merge_geometry() {
	util_mesh_merge_geometry();
}

void step_merge_geometry_check() {
	slot_layer_t *l = red();
	object_t     *o = script_get_context()->paint_object->base;
	int           n = script_object_triangle_count(o);
	check(n == 12 && script_layer_get_geometry_face_count(l, o) == 0 && !script_layer_get_geometry_face(l, o, tri[0]) && !script_layer_geometry_applies(l, o),
	      string("a mesh with a different triangle count leaves the faces out (%d triangles)", n));
	expect_bands("the faces of the changed mesh select nothing", 0, 0, 0, 0, 0, 0);
	script_layer_clear_geometry_faces(l);
	expect_bands("clear faces after the mesh change", 1, 1, 1, 1, 1, 1);
}

// 31, saved by the build before faces, the layer maps end three fields earlier
void step_fixture() {
	char     *file = string("%s/fixtures/p1_geometry_project.arm", root);
	buffer_t *json = script_load_blob(string("%s/fixtures/p1_geometry_project.json", root));
	check(json != NULL, "fixture expected values readable");
	if (json == NULL) {
		return;
	}
	any_map_t *expected = json_parse_to_map(sys_buffer_to_string(json));
	script_project_new();
	script_project_open(file);
	object_t *a = script_get_object("geo_a");
	object_t *b = script_get_object("geo_b");
	check(a != NULL && b != NULL && script_layer_count() == parse_int(any_map_get(expected, "layer_count")),
	      string("fixture opens with %d layers", script_layer_count()));
	if (a == NULL || b == NULL) {
		return;
	}
	char *names[5] = {"Layer 1", "fx_red", "fx_blue", "fx_green", "fx_group"};
	for (int i = 0; i < 5; ++i) {
		char         *key = string("layers.%s", names[i]);
		slot_layer_t *l   = layer_by_name(names[i]);
		int           ok  = l != NULL && script_layer_index(l) == parse_int(any_map_get(expected, string("%s.index", key))) &&
		         script_layer_get_geometry_mode(l) == parse_int(any_map_get(expected, string("%s.mode", key))) &&
		         script_layer_get_geometry_object(l, a) == parse_int(any_map_get(expected, string("%s.geo_a", key))) &&
		         script_layer_get_geometry_object(l, b) == parse_int(any_map_get(expected, string("%s.geo_b", key))) &&
		         script_layer_get_geometry_tile(l, 1002) == parse_int(any_map_get(expected, string("%s.tile_1002", key))) &&
		         script_layer_geometry_applies(l, a) == parse_int(any_map_get(expected, string("%s.applies_a", key))) &&
		         script_layer_geometry_applies(l, b) == parse_int(any_map_get(expected, string("%s.applies_b", key)));
		check(ok, string("fixture: layer %s keeps its geometry mask", names[i]));
		check(l != NULL && script_layer_get_geometry_face_count(l, a) == 0 && script_layer_get_geometry_face_count(l, b) == 0 &&
		          !script_layer_get_geometry_face(l, a, 0) && script_layer_geometry_face_applies(l, a, 0) == script_layer_geometry_applies(l, a),
		      string("fixture: layer %s has no faces", names[i]));
	}

	// Mean of the exported base color
	export_count += 1;
	char *dir = string("%s/p1_faces%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	float        mean[3] = {-1.0, -1.0, -1.0};
	any_array_t *files   = file_read_directory(dir);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, "_base.png")) {
			continue;
		}
		script_import_asset(string("%s/%s", dir, name), false);
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
			mean[0] = sum[0] / count;
			mean[1] = sum[1] / count;
			mean[2] = sum[2] / count;
		}
		script_texture_delete(name);
	}
	float r  = parse_float(any_map_get(expected, "base.r"));
	float g  = parse_float(any_map_get(expected, "base.g"));
	float bb = parse_float(any_map_get(expected, "base.b"));
	int   ok = absf(mean[0] - r) <= tolerance && absf(mean[1] - g) <= tolerance && absf(mean[2] - bb) <= tolerance;
	check(ok, string("fixture: exported base %.1f %.1f %.1f (expected %.1f %.1f %.1f)", mean[0], mean[1], mean[2], r, g, bb));
}

// Runs step st, returns the frames to wait before the next one or -1 after the last step
int run_step(int st) {
	if (st == 0) {
		step_build();
		return 2;
	}
	else if (st == 1) {
		step_remove_cube();
		return 2;
	}
	else if (st == 2) {
		step_layers();
		return 0;
	}
	else if (st == 3) {
		step_pick();
		return 0;
	}
	else if (st == 4) {
		step_triangle();
		return 0;
	}
	else if (st == 5) {
		step_polygon();
		return 0;
	}
	else if (st == 6) {
		step_exclude();
		return 0;
	}
	else if (st == 7) {
		step_remove();
		return 0;
	}
	else if (st == 8) {
		step_expand();
		return 0;
	}
	else if (st == 9) {
		step_objects();
		return 0;
	}
	else if (st == 10) {
		step_remove_empty();
		return 0;
	}
	else if (st == 11) {
		step_drag();
		return 0;
	}
	else if (st == 12) {
		step_tiles();
		return 0;
	}
	else if (st == 13) {
		step_group();
		return 0;
	}
	else if (st == 14) {
		step_effect();
		return 2;
	}
	else if (st == 15) {
		step_effect_check();
		return 0;
	}
	else if (st == 16) {
		step_viewport_include();
		return 0;
	}
	else if (st == 17) {
		step_viewport_exclude();
		return 0;
	}
	else if (st == 18) {
		step_select_tool();
		return 0;
	}
	else if (st == 19) {
		step_paint();
		return 0;
	}
	else if (st == 20) {
		step_duplicate();
		return 0;
	}
	else if (st == 21) {
		step_delete();
		return 0;
	}
	else if (st == 22) {
		step_flatten();
		return 0;
	}
	else if (st == 23) {
		step_merge();
		return 0;
	}
	else if (st == 24) {
		step_save();
		return 0;
	}
	else if (st == 25) {
		// project_save() writes on this frame
		return 0;
	}
	else if (st == 26) {
		step_reopen();
		return 2;
	}
	else if (st == 27) {
		step_rename();
		return 0;
	}
	else if (st == 28) {
		step_merge_geometry();
		return 2;
	}
	else if (st == 29) {
		step_merge_geometry_check();
		return 0;
	}
	else if (st == 30) {
		step_fixture();
		return 0;
	}
	return -1;
}

void next_frame() {
	if (wait > 0) {
		wait -= 1;
		script_notify_on_next_frame(next_frame);
		return;
	}
	step_again = 0;
	int w      = run_step(step);
	if (w < 0) {
		printf("[smoke] %d checks, %d failures", checks, failures);
		printf(failures == 0 && checks > 0 ? "SMOKE PASS" : "SMOKE FAIL");
		script_quit();
		return;
	}
	if (step_again) {
		sub += 1;
	}
	else {
		sub  = 0;
		wait = w;
		step += 1;
	}
	script_notify_on_next_frame(next_frame);
}

void main() {
	file_arm = string("%s/p1_geometry_faces.arm", dir_out);
	dir_exp  = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	script_notify_on_next_frame(next_frame);
}
