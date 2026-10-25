#include "global.h"

// LAY-05: Paint and Generator effects. A paint effect holds strokes painted over the element below it in its stack: on a
// gray base and a blue layer it is painted with fill, brush and eraser, undone, deleted, duplicated, pasted, saved and
// reopened, on the layer and on a black mask. Generators fill a mask or a layer through a node chain: on two quads, one
// facing up and one facing the side, and on a flat and a curved quad.
// Run with: base/tests/paint_headless/run.sh p1_effect_paint.c

char *root      = "../../../base/tests/paint_headless";
char *dir_out   = "../smoke";
char *file_arm  = NULL;
char *file_gen  = NULL;
char *dir_exp   = NULL;
float tolerance = 3.0;

int step         = 0;
int wait         = 0;
int checks       = 0;
int failures     = 0;
int export_count = 0;

object_t *cube      = NULL;
char     *cube_name = NULL; // The object is freed once removed

// Texels of a layer texture are sampled every stride, the exported textures have the same size
int         stride  = 1;
int         samples = 0;
u8_array_t *island  = NULL; // 1 inside the uv islands of the default cube, from a fill
u8_array_t *strokes = NULL; // Island texels the strokes cover
u8_array_t *bare    = NULL; // Island texels without strokes

int   count_before = 0;
int   count_layers = 0;
int   count_d      = 0; // Stroke texels of the paint effect of section D
float saved_r      = 0.0;
float saved_b      = 0.0;
int   saved_l      = 0;
int   saved_m      = 0;

// Quads facing up (left half of the uv space) and facing the default camera (right half)
char *mesh_gen = "o gen_up\n"
                 "v -1.0 -0.5 0.0\nv -0.1 -0.5 0.0\nv -0.1 0.5 0.0\nv -1.0 0.5 0.0\n"
                 "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.95\nvt 0.05 0.95\n"
                 "vn 0.0 0.0 1.0\n"
                 "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n"
                 "o gen_side\n"
                 "v 0.1 0.0 -0.5\nv 1.0 0.0 -0.5\nv 1.0 0.0 0.5\nv 0.1 0.0 0.5\n"
                 "vt 0.55 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.55 0.95\n"
                 "vn 0.0 -1.0 0.0\n"
                 "f 5/5/2 6/6/2 7/7/2\nf 5/5/2 7/7/2 8/8/2\n";

// A flat quad (left) and a curved one (right), its normals tilt from -x to +x
char *mesh_edges = "o edge_flat\n"
                   "v -1.0 -0.5 0.0\nv -0.1 -0.5 0.0\nv -0.1 0.5 0.0\nv -1.0 0.5 0.0\n"
                   "vt 0.05 0.05\nvt 0.45 0.05\nvt 0.45 0.95\nvt 0.05 0.95\n"
                   "vn 0.0 0.0 1.0\n"
                   "f 1/1/1 2/2/1 3/3/1\nf 1/1/1 3/3/1 4/4/1\n"
                   "o edge_curved\n"
                   "v 0.1 -0.5 0.0\nv 1.0 -0.5 0.0\nv 1.0 0.5 0.0\nv 0.1 0.5 0.0\n"
                   "vt 0.55 0.05\nvt 0.95 0.05\nvt 0.95 0.95\nvt 0.55 0.95\n"
                   "vn -0.7 0.0 0.714\nvn 0.7 0.0 0.714\n"
                   "f 5/5/2 6/6/3 7/7/3\nf 5/5/2 7/7/3 8/8/2\n";

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

// The paint effect of a layer, mask 1 for the one of a mask of the layer
slot_layer_t *paint_effect_of(slot_layer_t *parent, int mask) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->parent == parent && l->effect_paint && l->effect == mask) {
			return l;
		}
	}
	return NULL;
}

// Layer names from the bottom up
char *order() {
	char *s = "";
	for (int i = 0; i < script_layer_count(); ++i) {
		s = string("%s%s%s", s, i > 0 ? " " : "", script_layer_at(i)->name);
	}
	return s;
}

int no_paint_effect() {
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->effect_paint) {
			return 0;
		}
	}
	return 1;
}

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

// Alpha of the sampled texels, render targets can be BGRA but alpha stays last
u8_array_t *alpha_samples(gpu_texture_t *t) {
	buffer_t *pixels = script_get_texture_pixels(t);
	int       n      = pixels->length / 4;
	stride           = n / 16384 + 1;
	samples          = (n + stride - 1) / stride;
	u8_array_t *a    = u8_array_create(samples);
	for (int k = 0; k < samples; ++k) {
		a->buffer[k] = buffer_get_u8(pixels, k * stride * 4 + 3);
	}
	return a;
}

// Sampled texels whose alpha is above min
int count_above(gpu_texture_t *t, int min) {
	u8_array_t *a     = alpha_samples(t);
	int         count = 0;
	for (int k = 0; k < samples; ++k) {
		if (a->buffer[k] > min) {
			count += 1;
		}
	}
	return count;
}

// Sum of the sampled bytes, to see a texture did not change
int texture_sum(gpu_texture_t *t) {
	buffer_t *pixels = script_get_texture_pixels(t);
	int       n      = pixels->length / 4;
	int       st     = n / 16384 + 1;
	int       sum    = 0;
	for (int p = 0; p < n; p += st) {
		sum += buffer_get_u8(pixels, p * 4) + buffer_get_u8(pixels, p * 4 + 1) + buffer_get_u8(pixels, p * 4 + 2) + buffer_get_u8(pixels, p * 4 + 3);
	}
	return sum;
}

// Largest value of a mask texture, masks write red only so this is safe under BGRA
int max_value(gpu_texture_t *t) {
	buffer_t *pixels = script_get_texture_pixels(t);
	int       n      = pixels->length / 4;
	int       st     = n / 16384 + 1;
	int       m      = 0;
	for (int p = 0; p < n; p += st) {
		int r = buffer_get_u8(pixels, p * 4);
		int b = buffer_get_u8(pixels, p * 4 + 2);
		m     = r > m ? r : m;
		m     = b > m ? b : m;
	}
	return m;
}

float val_at(gpu_texture_t *t, float u, float v) {
	buffer_t *pixels = script_get_texture_pixels(t);
	int       p      = ((int)(v * t->height) * t->width + (int)(u * t->width)) * 4;
	int       r      = buffer_get_u8(pixels, p);
	int       b      = buffer_get_u8(pixels, p + 2);
	return r > b ? r : b;
}

// Mean and deviation of the value of a mask texture over a uv rectangle
float region_mean = 0.0;
float region_dev  = 0.0;

void region_stats(gpu_texture_t *t, float u0, float v0, float u1, float v1) {
	buffer_t *pixels = script_get_texture_pixels(t);
	float     sum    = 0.0;
	float     sum2   = 0.0;
	int       count  = 0;
	for (int i = 0; i < 32; ++i) {
		for (int j = 0; j < 32; ++j) {
			float u = u0 + (u1 - u0) * (i + 0.5) / 32.0;
			float v = v0 + (v1 - v0) * (j + 0.5) / 32.0;
			int   p = ((int)(v * t->height) * t->width + (int)(u * t->width)) * 4;
			int   r = buffer_get_u8(pixels, p);
			int   b = buffer_get_u8(pixels, p + 2);
			float x = r > b ? r : b;
			sum += x;
			sum2 += x * x;
			count += 1;
		}
	}
	region_mean = sum / count;
	float var   = sum2 / count - region_mean * region_mean;
	region_dev  = sqrt(var > 0.0 ? var : 0.0);
}

// Stroke and bare texels of the island from the strokes of a paint effect
void make_sets(slot_layer_t *e) {
	u8_array_t *a  = alpha_samples(e->texpaint);
	strokes        = u8_array_create(samples);
	bare           = u8_array_create(samples);
	int in_island  = 0;
	int in_strokes = 0;
	int in_bare    = 0;
	for (int k = 0; k < samples; ++k) {
		int in             = k < island->length && island->buffer[k] == 1;
		strokes->buffer[k] = in && a->buffer[k] > 250 ? 1 : 0;
		bare->buffer[k]    = in && a->buffer[k] == 0 ? 1 : 0;
		in_island += in;
		in_strokes += strokes->buffer[k];
		in_bare += bare->buffer[k];
	}
	check(in_strokes > in_island / 1000 && in_strokes < in_island * 9 / 10 && in_bare > 0,
	      string("the stroke covers part of the island (%d of %d texels, %d bare)", in_strokes, in_island, in_bare));
}

// Mean of the sampled alpha over the texels of a set
float set_alpha(gpu_texture_t *t, u8_array_t *set) {
	u8_array_t *a     = alpha_samples(t);
	float       sum   = 0.0;
	int         count = 0;
	for (int k = 0; k < samples && k < set->length; ++k) {
		if (set->buffer[k] == 1) {
			sum += a->buffer[k];
			count += 1;
		}
	}
	return count > 0 ? sum / count : -1.0;
}

// Exported base color: mean red and blue over the sampled texels of set (all of them when NULL), and red and blue at two
// uv points a and b
float exp_r = -1.0;
float exp_b = -1.0;
float ar    = -1.0;
float ab    = -1.0;
float br    = -1.0;
float bb    = -1.0;

void export_base(u8_array_t *set, float ua, float va, float ub, float vb) {
	exp_r = -1.0;
	exp_b = -1.0;
	export_count += 1;
	char *dir = string("%s/p1_effect_paint%d", dir_exp, export_count);
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
			int   n     = pixels->length / 4;
			int   st    = n / 16384 + 1;
			float sr    = 0.0;
			float sb    = 0.0;
			int   count = 0;
			for (int k = 0; k * st < n; ++k) {
				if (set != NULL && (k >= set->length || set->buffer[k] == 0)) {
					continue;
				}
				sr += buffer_get_u8(pixels, k * st * 4);
				sb += buffer_get_u8(pixels, k * st * 4 + 2);
				count += 1;
			}
			exp_r  = count > 0 ? sr / count : -1.0;
			exp_b  = count > 0 ? sb / count : -1.0;
			int w  = tex->width;
			int h  = tex->height;
			int pa = ((int)(va * h) * w + (int)(ua * w)) * 4;
			int pb = ((int)(vb * h) * w + (int)(ub * w)) * 4;
			ar     = buffer_get_u8(pixels, pa);
			ab     = buffer_get_u8(pixels, pa + 2);
			br     = buffer_get_u8(pixels, pb);
			bb     = buffer_get_u8(pixels, pb + 2);
		}
		script_texture_delete(name);
	}
}

void expect_set(u8_array_t *set, float r, float b, char *when) {
	export_base(set, 0.5, 0.5, 0.5, 0.5);
	check(absf(exp_r - r) <= tolerance && absf(exp_b - b) <= tolerance, string("%s: base %.0f %.0f (expected %.0f %.0f)", when, exp_r, exp_b, r, b));
}

// Red and blue in the middle of the left and of the right half of the uv space
void expect_halves(float lr, float lb, float rr, float rb, char *when) {
	export_base(NULL, 0.25, 0.5, 0.75, 0.5);
	int ok = absf(ar - lr) <= tolerance && absf(ab - lb) <= tolerance && absf(br - rr) <= tolerance && absf(bb - rb) <= tolerance;
	check(ok, string("%s: left %.0f %.0f (expected %.0f %.0f), right %.0f %.0f (expected %.0f %.0f)", when, ar, ab, lr, lb, br, bb, rr, rb));
}

slot_material_t *color_material(char *name, float r, float g, float b) {
	slot_material_t *m = script_material_create(name);
	ui_node_t       *n = script_material_create_node_at("RGB", -600.0, 0.0);
	script_material_set_color(n, 0, 0, r, g, b, 1.0);
	script_material_connect(n, 0, script_material_get_node("OUTPUT_MATERIAL_PBR"), 0);
	script_material_update();
	return m;
}

void fill_on(slot_layer_t *l) {
	script_layer_set(l);
	context_select_tool(TOOL_TYPE_FILL);
	script_paint(0.5, 0.5);
	script_paint_end();
	context_select_tool(TOOL_TYPE_BRUSH);
}

void stroke_on(slot_layer_t *l, int tool) {
	script_layer_set(l);
	context_select_tool(tool);
	script_paint(0.35, 0.5);
	script_paint(0.5, 0.5);
	script_paint(0.65, 0.5);
	script_paint_end();
	context_select_tool(TOOL_TYPE_BRUSH);
}

// Levels with Out Black 1 and Out White 0 inverts its input
void set_invert(slot_layer_t *e) {
	script_material_set(e->fill_material);
	ui_node_t *n = script_material_get_node("LEVELS");
	script_material_set_float(n, 1, 4, 1.0);
	script_material_set_float(n, 1, 5, 0.0);
	script_material_update();
}

slot_layer_t *fx_setup() {
	script_project_new();
	slot_layer_t *base = script_layer_new_color(0xff808080, 1.0, 0.5, 0.0);
	slot_layer_t *l    = script_layer_new_color(0xff3366cc, 1.0, 0.5, 0.0);
	script_layer_set_name(base, "fx_base");
	script_layer_set_name(l, "fx_l");
	return l;
}

// A. Layer paint effect

void step_a_create() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	slot_layer_t *l = fx_setup();
	slot_layer_t *e = script_layer_add_effect(l, EFFECT_TYPE_PAINT);
	check(e != NULL && e->effect_paint && !e->effect && e->parent == l && e->fill_material == NULL && script_layer_index(e) == script_layer_index(l) - 1 &&
	          e->texpaint_result != NULL,
	      "paint effect right below its layer, without material, with a result");
	if (e == NULL) {
		return;
	}
	check(count_above(e->texpaint, 0) == 0, "the strokes start transparent");
	expect_set(NULL, 51, 204, "an empty paint effect shows the layer");
}

void step_a_fill() {
	slot_layer_t *l = layer_by_name("fx_l");
	slot_layer_t *e = layer_by_name("Paint");
	color_material("pe_red", 1.0, 0.0, 0.0);
	int before = texture_sum(l->texpaint);
	fill_on(e);
	u8_array_t *a      = alpha_samples(e->texpaint);
	int         inside = 0;
	island             = u8_array_create(samples);
	for (int k = 0; k < samples; ++k) {
		island->buffer[k] = a->buffer[k] > 250 ? 1 : 0;
		inside += island->buffer[k];
	}
	check(inside > samples / 10 && inside < samples, string("the fill covers the uv islands (%d of %d texels)", inside, samples));
	check(texture_sum(l->texpaint) == before, "the layer below keeps its pixels");
	expect_set(island, 255, 0, "strokes over the content");
	script_layer_set_channel(e, LAYER_CH_BASE, BLEND_TYPE_MIX, 0.5);
	expect_set(island, 153, 102, "strokes at half opacity");
	script_undo();
}

void step_a_undo() {
	expect_set(island, 255, 0, "undo of the opacity");
	script_undo(); // The fill
	check(count_above(layer_by_name("Paint")->texpaint, 0) == 0, "undo of the fill empties the strokes");
}

void step_a_undone() {
	expect_set(island, 51, 204, "undo of the fill shows the layer");
	script_redo();
	check(count_above(layer_by_name("Paint")->texpaint, 250) > 0, "redo of the fill gives the strokes back");
}

void step_a_redone() {
	expect_set(island, 255, 0, "redo of the fill");
	// Half opacity strokes over a copy of themselves: the copy reads the original
	slot_layer_t *e = layer_by_name("Paint");
	script_layer_set_channel(e, LAYER_CH_BASE, BLEND_TYPE_MIX, 0.5);
	int           count = script_layer_count();
	slot_layer_t *copy  = script_layer_duplicate(e);
	check(script_layer_count() == count + 1 && copy != NULL && copy != e && copy->effect_paint && script_layer_index(copy) == script_layer_index(e) + 1,
	      "duplicate of the paint effect above it");
	expect_set(island, 204, 51, "the copy of the paint effect reads the original");
	script_undo();
}

void step_a_duplicate_undone() {
	expect_set(island, 153, 102, "undo of the duplicate");
}

// B. The effects below stay non destructive

void step_b_build() {
	slot_layer_t *l  = fx_setup();
	slot_layer_t *lv = script_layer_add_effect(l, EFFECT_TYPE_LEVELS);
	script_layer_set_name(lv, "fx_lv");
	set_invert(lv);
	slot_layer_t *e = script_layer_add_effect(l, EFFECT_TYPE_PAINT);
	check(e != NULL && script_layer_index(e) == script_layer_index(lv) + 1, "paint effect on top of the levels");
	if (e == NULL) {
		return;
	}
	script_layer_set_name(e, "pe_b");
	color_material("pe_red_b", 1.0, 0.0, 0.0);
	stroke_on(e, TOOL_TYPE_BRUSH);
	make_sets(e);
	expect_set(strokes, 255, 0, "strokes over the inverted layer");
	expect_set(bare, 204, 51, "around the strokes the inverted layer");
	script_layer_set_visible(lv, false);
}

void step_b_hidden() {
	expect_set(strokes, 255, 0, "the strokes stay on top when the effect below changes");
	expect_set(bare, 51, 204, "around them the layer without the hidden levels");
	slot_layer_t *top = script_layer_add_effect(layer_by_name("fx_l"), EFFECT_TYPE_LEVELS);
	script_layer_set_name(top, "fx_top");
	set_invert(top);
	expect_set(strokes, 0, 255, "the effect above reads the result");
	expect_set(bare, 204, 51, "the effect above inverts the layer around the strokes");
	script_layer_delete(top);
}

void step_b_deleted() {
	expect_set(bare, 51, 204, "deleting the effect above");
	check(layer_by_name("fx_top") == NULL, "the effect above is deleted");
}

// C. Eraser and undo

void step_c_erase() {
	slot_layer_t *e = layer_by_name("pe_b");
	count_before    = count_above(e->texpaint, 250);
	stroke_on(e, TOOL_TYPE_ERASER);
	float a = set_alpha(e->texpaint, strokes);
	check(a < 5.0, string("the eraser clears the strokes (alpha %.1f)", a));
}

void step_c_erased() {
	expect_set(strokes, 51, 204, "erased strokes show the input");
	script_undo();
	int count = count_above(layer_by_name("pe_b")->texpaint, 250);
	check(count == count_before, string("undo of the eraser gives the strokes back (%d of %d)", count, count_before));
}

void step_c_undone() {
	expect_set(strokes, 255, 0, "undo of the eraser");
	script_redo();
	int erased = count_above(layer_by_name("pe_b")->texpaint, 250);
	script_undo();
	int count = count_above(layer_by_name("pe_b")->texpaint, 250);
	check(erased < count_before / 10 && count == count_before, string("redo and undo of the eraser (%d, %d of %d)", erased, count, count_before));
}

void step_c_again() {
	expect_set(strokes, 255, 0, "after redo and undo of the eraser");
}

// D. Undo of create and delete

void step_d_create() {
	slot_layer_t *l     = fx_setup();
	int           count = script_layer_count();
	slot_layer_t *e     = script_layer_add_effect(l, EFFECT_TYPE_PAINT);
	check(e != NULL && script_layer_count() == count + 1, "paint effect created");
	script_undo();
	check(script_layer_count() == count && no_paint_effect(), string("undo removes the paint effect (%d layers)", script_layer_count()));
	count_layers = count;
}

void step_d_redo() {
	script_redo();
	slot_layer_t *e = layer_by_name("Paint");
	check(e != NULL && e->effect_paint && e->texpaint_result != NULL && e->parent == layer_by_name("fx_l") && script_layer_count() == count_layers + 1,
	      "redo brings the paint effect back");
	check(e != NULL && count_above(e->texpaint, 0) == 0, "with empty strokes");
}

void step_d_redone() {
	expect_set(island, 51, 204, "after redo the empty paint effect shows the layer");
	slot_layer_t *e = layer_by_name("Paint");
	color_material("pe_red_d", 1.0, 0.0, 0.0);
	stroke_on(e, TOOL_TYPE_BRUSH);
	make_sets(e);
	count_d = count_above(e->texpaint, 250);
	expect_set(strokes, 255, 0, "strokes on the paint effect");
	int count = script_layer_count();
	script_layer_delete(e);
	check(script_layer_count() == count - 1 && no_paint_effect(), "delete the paint effect");
	expect_set(strokes, 51, 204, "deleting the paint effect shows the layer");
	script_undo();
}

void step_d_undone() {
	slot_layer_t *e     = layer_by_name("Paint");
	int           count = e != NULL ? count_above(e->texpaint, 250) : -1;
	check(e != NULL && e->effect_paint && e->texpaint_result != NULL && e->parent == layer_by_name("fx_l") && count == count_d,
	      string("undo of the delete gives the paint effect back (%d of %d stroke texels)", count, count_d));
	expect_set(strokes, 255, 0, "the strokes are visible again");
}

// E. Duplicate and paste

void step_e_duplicate() {
	slot_layer_t *l     = layer_by_name("fx_l");
	slot_layer_t *e     = layer_by_name("Paint");
	int           count = script_layer_count();
	slot_layer_t *copy  = script_layer_duplicate(l);
	slot_layer_t *ce    = copy != NULL ? paint_effect_of(copy, 0) : NULL;
	check(script_layer_count() == count + 2, string("duplicating the layer copies its paint effect (%d to %d layers)", count, script_layer_count()));
	check(ce != NULL && ce != e && ce->texpaint != e->texpaint && ce->texpaint_result != NULL && ce->texpaint_result != e->texpaint_result &&
	          count_above(ce->texpaint, 250) == count_d,
	      "the copy has its own strokes and result");
	script_undo();
	check(script_layer_count() == count, "undo of the duplicate");
}

void step_e_paste() {
	script_layer_set(layer_by_name("fx_l"));
	slot_layer_t *p = script_layer_new_color(0xff204080, 1.0, 0.5, 0.0); // On top, the composite shows it
	script_layer_set_name(p, "fx_p");
	int count = script_layer_count();
	check(script_layer_paste(layer_by_name("Paint"), p), "paste the paint effect into another layer");
	slot_layer_t *pe = script_layer_at(script_layer_index(p) - 1);
	check(script_layer_count() == count + 1 && pe != NULL && pe->parent == p && pe->effect_paint && pe != layer_by_name("Paint") &&
	          count_above(pe->texpaint, 250) == count_d,
	      "the pasted paint effect has the strokes");
	count_layers = count;
}

void step_e_pasted() {
	expect_set(strokes, 255, 0, "the pasted strokes over the other layer");
	expect_set(bare, 32, 128, "around them the other layer");
	script_undo(); // The strokes
	script_undo(); // The paint effect
	check(script_layer_count() == count_layers && paint_effect_of(layer_by_name("fx_p"), 0) == NULL, "two undos remove the pasted paint effect");
	script_redo();
	script_redo();
	slot_layer_t *pe    = paint_effect_of(layer_by_name("fx_p"), 0);
	int           count = pe != NULL ? count_above(pe->texpaint, 250) : -1;
	check(pe != NULL && count == count_d, string("two redos give the pasted strokes back (%d of %d)", count, count_d));
}

void step_e_redone() {
	expect_set(strokes, 255, 0, "redo of the paste");
	expect_set(bare, 32, 128, "redo of the paste, around the strokes");
	slot_layer_t *m = script_layer_new_mask(layer_by_name("fx_p"), 0); // Hides the other layer again
	check(!script_layer_paste(layer_by_name("Paint"), m), "a layer paint effect does not go into a mask");
}

// Flatten and Merge Group bake the result of a paint effect
void step_e_flatten() {
	slot_layer_t *g = script_layer_new_group(layer_by_name("fx_l"));
	check(g != NULL, "group of the layer with a paint effect");
	if (g == NULL) {
		return;
	}
	script_layer_set_name(g, "fx_g");
	slot_layer_t *flat = script_layer_flatten_group(g);
	check(flat != NULL && !flat->effect_paint && paint_effect_of(flat, 0) == NULL && layer_by_name("Paint") != NULL,
	      "flatten makes a layer and keeps the paint effect in the group");
	expect_set(strokes, 255, 0, "flattened strokes");
	expect_set(bare, 51, 204, "flattened, around the strokes");
	script_undo();
}

void step_e_merge() {
	slot_layer_t *g = layer_by_name("fx_g");
	check(g != NULL && g->visible, "undo of the flatten");
	slot_layer_t *merged = g != NULL ? script_layer_merge_group(g) : NULL;
	check(merged != NULL && !merged->effect_paint && layer_by_name("Paint") == NULL && paint_effect_of(merged, 0) == NULL,
	      "merge group bakes the paint effect");
	expect_set(strokes, 255, 0, "merged strokes");
	expect_set(bare, 51, 204, "merged, around the strokes");
}

// F. Mask paint effect

void step_f_mask() {
	slot_layer_t *l = fx_setup();
	slot_layer_t *m = script_layer_new_mask(l, 0);
	script_layer_set_name(m, "fx_m");
	slot_layer_t *e = script_layer_add_effect(m, EFFECT_TYPE_PAINT);
	check(e != NULL && e->effect && e->effect_paint && e->parent == l && e->fill_material == NULL && script_layer_index(e) == script_layer_index(m) + 1,
	      "paint effect right above its mask");
	if (e == NULL) {
		return;
	}
	script_layer_set_name(e, "pe_m");
	expect_set(island, 128, 128, "an empty mask paint effect keeps the black mask");
	color_material("pe_white", 1.0, 1.0, 1.0);
	fill_on(e);
	expect_set(island, 51, 204, "white strokes show the layer");
	check(max_value(m->texpaint) == 0, "the mask is untouched");
	script_layer_clear(e);
	check(count_above(e->texpaint, 0) == 0, "clear removes the strokes");
	expect_set(island, 128, 128, "cleared strokes");
	script_undo();
}

void step_f_fill_below() {
	expect_set(island, 51, 204, "undo of the clear");
	slot_layer_t *m    = layer_by_name("fx_m");
	slot_layer_t *e    = layer_by_name("pe_m");
	slot_layer_t *fill = script_layer_add_effect(m, EFFECT_TYPE_FILL);
	check(fill != NULL && script_layer_move(fill, script_layer_index(e)) && script_layer_index(fill) == script_layer_index(e) - 1 &&
	          script_layer_index(fill) == script_layer_index(m) + 1,
	      "fill effect moved below the paint effect");
	script_layer_clear(e);
	expect_set(island, 51, 204, "the white fill below shows through the empty strokes");
	color_material("pe_black", 0.0, 0.0, 0.0);
	stroke_on(e, TOOL_TYPE_BRUSH);
	make_sets(e);
	expect_set(strokes, 128, 128, "black strokes hide the layer");
	expect_set(bare, 51, 204, "around them the fill below");
	stroke_on(e, TOOL_TYPE_ERASER);
}

void step_f_erased() {
	expect_set(strokes, 51, 204, "the eraser on the mask strokes shows the fill again");
	script_undo();
}

void step_f_duplicate() {
	slot_layer_t *e      = layer_by_name("pe_m");
	int           count  = count_above(e->texpaint, 250);
	int           layers = script_layer_count();
	slot_layer_t *copy   = script_layer_duplicate(layer_by_name("fx_l"));
	slot_layer_t *ce     = copy != NULL ? paint_effect_of(copy, 1) : NULL;
	int           copied = ce != NULL ? count_above(ce->texpaint, 250) : -1;
	check(count > 0 && ce != NULL && ce != e && copied == count && script_layer_count() == layers + 4,
	      string("duplicating the layer keeps the coverage of the mask strokes (%d of %d)", copied, count));
	script_undo();
}

// G. Save and reopen

void step_g_save() {
	expect_set(strokes, 128, 128, "undo of the eraser and of the duplicate");
	slot_layer_t *e = script_layer_add_effect(layer_by_name("fx_l"), EFFECT_TYPE_PAINT);
	script_layer_set_name(e, "pe_l");
	color_material("pe_red_g", 1.0, 0.0, 0.0);
	stroke_on(e, TOOL_TYPE_BRUSH);
	saved_l = count_above(e->texpaint, 250);
	saved_m = count_above(layer_by_name("pe_m")->texpaint, 250);
	check(saved_l > 0 && saved_m > 0, "strokes on a layer and on a mask paint effect");
	export_base(island, 0.5, 0.5, 0.5, 0.5);
	saved_r = exp_r;
	saved_b = exp_b;
	if (iron_file_exists(file_arm)) {
		iron_delete_file(file_arm);
	}
	project_filepath_set(file_arm);
	project_save(false); // Writes on the next frame
}

void step_g_reopen() {
	check(iron_file_exists(file_arm), "project saved");
	script_project_new();
	script_project_open(file_arm);
}

void step_g_reopened() {
	slot_layer_t *pl = layer_by_name("pe_l");
	slot_layer_t *pm = layer_by_name("pe_m");
	int           cl = pl != NULL ? count_above(pl->texpaint, 250) : -1;
	int           cm = pm != NULL ? count_above(pm->texpaint, 250) : -1;
	check(pl != NULL && pl->effect_paint && !pl->effect && pl->texpaint_result != NULL && cl == saved_l,
	      string("the layer paint effect is saved (%d of %d stroke texels)", cl, saved_l));
	check(pm != NULL && pm->effect_paint && pm->effect && pm->texpaint_result != NULL && cm == saved_m,
	      string("the mask paint effect is saved with the coverage of its strokes (%d of %d)", cm, saved_m));
	expect_set(island, saved_r, saved_b, "reopened");
}

// The layer paint effect and the paint effect on top of the mask stack are baked before merging
void step_g_merge() {
	slot_layer_t *g      = script_layer_new_group(layer_by_name("fx_l"));
	slot_layer_t *merged = g != NULL ? script_layer_merge_group(g) : NULL;
	// Left: the default layer, the base and the merged layer
	check(merged != NULL && no_paint_effect() && script_layer_count() == 3, string("merge group of the layer with its paint effects (%s)", order()));
	expect_set(island, saved_r, saved_b, "merged with its paint effects");
}

// Saved by the build before the paint effect, the layer maps end one field earlier
void step_g_v22() {
	script_project_new();
	script_project_open(string("%s/fixtures/p1_v22_project.arm", root));
	check(script_layer_count() == 6 && no_paint_effect(), string("v22 fixture opens with %d layers and no paint effect", script_layer_count()));
}

// H. Generators

void step_h_build(char *mesh) {
	script_project_new();
	cube      = script_get_context()->paint_object->base;
	cube_name = string_copy(cube->name);
	script_append_mesh_obj(mesh);
}

void step_h_remove_cube() {
	script_object_remove(cube);
}

int material_has(slot_material_t *m, char *type) {
	script_material_set(m);
	return script_material_get_node(type) != NULL;
}

void set_range(slot_layer_t *g, float from_min, float from_max) {
	script_material_set(g->fill_material);
	ui_node_t *n = script_material_get_node("MAPRANGE");
	script_material_set_float(n, 1, 1, from_min);
	script_material_set_float(n, 1, 2, from_max);
	script_material_update();
}

void step_h_presets() {
	check(script_get_object(cube_name) == NULL && script_get_object("gen_up") != NULL && script_get_object("gen_side") != NULL,
	      "quads replace the default mesh");
	slot_layer_t *base = script_layer_new_color(0xff808080, 1.0, 0.5, 0.0);
	slot_layer_t *l    = script_layer_new_color(0xff3366cc, 1.0, 0.5, 0.0);
	script_layer_set_name(base, "fx_base");
	script_layer_set_name(l, "fx_l");
	slot_layer_t *edges = script_layer_add_effect(l, EFFECT_TYPE_GENERATOR);
	check(edges != NULL && starts_with(edges->name, "Edges") && edges->fill_material != NULL && material_has(edges->fill_material, "MIX_RGB") &&
	          material_has(edges->fill_material, "MAPRANGE") && material_has(edges->fill_material, "BAKE_CURVATURE"),
	      string("the generator effect starts with the Edges preset (%s)", edges != NULL ? edges->name : "none"));
	script_layer_delete(edges);
	slot_layer_t *m = script_layer_new_mask(l, 0);
	script_layer_set_name(m, "fx_m");
	count_layers    = script_layer_count();
	slot_layer_t *g = script_layer_add_generator(m, GENERATOR_TYPE_FACING_UP);
	check(g != NULL && g->effect && g->parent == l && script_layer_index(g) == script_layer_index(m) + 1 && starts_with(g->name, "Facing Up"),
	      "facing up generator on the mask");
	if (g == NULL) {
		return;
	}
	expect_halves(51, 204, 128, 128, "facing up: the layer shows on the quad facing up");
	set_range(g, 1.5, 2.0);
	expect_halves(128, 128, 128, 128, "parameters refill the effect");
	script_undo(); // The mask effect
	script_undo(); // Its material
	check(script_layer_count() == count_layers, "undo of the generator");
	script_undo(); // The black mask
}

void step_h_layer() {
	slot_layer_t *l = layer_by_name("fx_l");
	check(layer_by_name("fx_m") == NULL, "undo of the black mask");
	slot_layer_t *g = script_layer_add_generator(l, GENERATOR_TYPE_FACING_UP);
	check(g != NULL && !g->effect && g->parent == l, "facing up generator on the layer");
	expect_halves(255, 255, 51, 204, "facing up on the layer: white on the quad facing up");

	slot_layer_t *m  = script_layer_new_mask(l, 0);
	slot_layer_t *gg = script_layer_add_generator(m, GENERATOR_TYPE_GRADIENT);
	check(gg != NULL && starts_with(gg->name, "3D Gradient"), "3D gradient generator");
	if (gg != NULL) {
		float low  = val_at(gg->texpaint, 0.75, 0.2);
		float high = val_at(gg->texpaint, 0.75, 0.8);
		check(absf(low - high) > 50.0, string("the 3D gradient follows the height of the side quad (%.0f, %.0f)", low, high));
	}

	slot_layer_t *m2 = script_layer_new_mask(l, 0);
	slot_layer_t *gn = script_layer_add_generator(m2, GENERATOR_TYPE_NOISE);
	check(gn != NULL && starts_with(gn->name, "Noise"), "noise generator");
	if (gn != NULL) {
		region_stats(gn->texpaint, 0.1, 0.1, 0.4, 0.9);
		check(region_mean >= 30.0 && region_mean <= 225.0 && region_dev > 8.0,
		      string("noise over the quad (mean %.0f, deviation %.1f)", region_mean, region_dev));
	}
}

void step_h_edges() {
	check(script_get_object(cube_name) == NULL && script_get_object("edge_curved") != NULL, "flat and curved quads replace the default mesh");
	slot_layer_t *base = script_layer_new_color(0xff808080, 1.0, 0.5, 0.0);
	slot_layer_t *l    = script_layer_new_color(0xff3366cc, 1.0, 0.5, 0.0);
	script_layer_set_name(base, "fx_base");
	script_layer_set_name(l, "fx_l");
	slot_layer_t *m = script_layer_new_mask(l, 0);
	slot_layer_t *g = script_layer_add_generator(m, GENERATOR_TYPE_EDGES);
	check(g != NULL, "edges generator on the mask");
	if (g == NULL) {
		return;
	}
	script_layer_set_name(g, "gen_edges");
	set_range(g, 0.0, 0.1);
	region_stats(g->texpaint, 0.1, 0.1, 0.4, 0.9);
	float flat = region_mean;
	region_stats(g->texpaint, 0.6, 0.1, 0.9, 0.9);
	float curved = region_mean;
	check(curved > flat + 40.0, string("edges: the curved quad is brighter than the flat one (%.0f, %.0f)", curved, flat));
	export_base(NULL, 0.25, 0.5, 0.75, 0.5);
	saved_r = exp_r;
	saved_b = exp_b;
	if (iron_file_exists(file_gen)) {
		iron_delete_file(file_gen);
	}
	project_filepath_set(file_gen);
	project_save(false);
}

void step_h_reopen() {
	check(iron_file_exists(file_gen), "project with a generator saved");
	script_project_new();
	script_project_open(file_gen);
}

void step_h_reopened() {
	expect_set(NULL, saved_r, saved_b, "reopened with a generator");
	slot_layer_t *g = layer_by_name("gen_edges");
	slot_layer_t *o = script_layer_new_color(0xff204080, 1.0, 0.5, 0.0);
	slot_layer_t *m = script_layer_new_mask(o, 0);
	check(g != NULL && script_layer_paste(g, m), "paste the generator into another mask");
	slot_layer_t *copy = script_layer_at(script_layer_index(m) + 1);
	check(g != NULL && copy != NULL && copy->effect && copy->parent == o && copy->fill_material != NULL && copy->fill_material != g->fill_material,
	      "the pasted generator has its own material");
}

// Runs step st, returns the frames to wait before the next one or -1 after the last step
int run_step(int st) {
	if (st == 0) {
		step_a_create();
	}
	else if (st == 1) {
		step_a_fill();
	}
	else if (st == 2) {
		step_a_undo();
	}
	else if (st == 3) {
		step_a_undone();
	}
	else if (st == 4) {
		step_a_redone();
	}
	else if (st == 5) {
		step_a_duplicate_undone();
	}
	else if (st == 6) {
		step_b_build();
	}
	else if (st == 7) {
		step_b_hidden();
	}
	else if (st == 8) {
		step_b_deleted();
	}
	else if (st == 9) {
		step_c_erase();
	}
	else if (st == 10) {
		step_c_erased();
	}
	else if (st == 11) {
		step_c_undone();
	}
	else if (st == 12) {
		step_c_again();
	}
	else if (st == 13) {
		step_d_create();
	}
	else if (st == 14) {
		step_d_redo();
	}
	else if (st == 15) {
		step_d_redone();
	}
	else if (st == 16) {
		step_d_undone();
	}
	else if (st == 17) {
		step_e_duplicate();
	}
	else if (st == 18) {
		step_e_paste();
	}
	else if (st == 19) {
		step_e_pasted();
	}
	else if (st == 20) {
		step_e_redone();
	}
	else if (st == 21) {
		step_e_flatten();
	}
	else if (st == 22) {
		step_e_merge();
	}
	else if (st == 23) {
		step_f_mask();
	}
	else if (st == 24) {
		step_f_fill_below();
	}
	else if (st == 25) {
		step_f_erased();
	}
	else if (st == 26) {
		step_f_duplicate();
	}
	else if (st == 27) {
		step_g_save();
	}
	else if (st == 28) {
		// project_save() writes on this frame
	}
	else if (st == 29) {
		step_g_reopen();
	}
	else if (st == 30) {
		step_g_reopened();
	}
	else if (st == 31) {
		step_g_merge();
	}
	else if (st == 32) {
		step_g_v22();
	}
	else if (st == 33) {
		step_h_build(mesh_gen);
		return 2;
	}
	else if (st == 34) {
		step_h_remove_cube();
		return 2;
	}
	else if (st == 35) {
		step_h_presets();
	}
	else if (st == 36) {
		step_h_layer();
	}
	else if (st == 37) {
		step_h_build(mesh_edges);
		return 2;
	}
	else if (st == 38) {
		step_h_remove_cube();
		return 2;
	}
	else if (st == 39) {
		step_h_edges();
	}
	else if (st == 40) {
		// project_save() writes on this frame
	}
	else if (st == 41) {
		step_h_reopen();
	}
	else if (st == 42) {
		step_h_reopened();
	}
	else {
		return -1;
	}
	return 0;
}

void next_frame() {
	if (wait > 0) {
		wait -= 1;
		script_notify_on_next_frame(next_frame);
		return;
	}
	int w = run_step(step);
	if (w < 0) {
		printf("[smoke] %d checks, %d failures", checks, failures);
		printf(failures == 0 && checks > 0 ? "SMOKE PASS" : "SMOKE FAIL");
		script_quit();
		return;
	}
	wait = w;
	step += 1;
	script_notify_on_next_frame(next_frame);
}

void main() {
	file_arm = string("%s/p1_effect_paint.arm", dir_out);
	file_gen = string("%s/p1_effect_generator.arm", dir_out);
	dir_exp  = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	script_notify_on_next_frame(next_frame);
}
