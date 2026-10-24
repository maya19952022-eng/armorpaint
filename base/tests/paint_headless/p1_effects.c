#include "global.h"

// LAY-05: effect stacks. A layer gets two Levels effects that each invert the color, a hidden
// effect is skipped by the next one. A black mask of the layer gets a Fill effect (white) and a
// Levels effect on top. Checks the exported composite, undo, delete, save and reopen.
// Run with: base/tests/paint_headless/run.sh p1_effects.c

char *dir_out   = "../smoke";
char *file_save = NULL;
char *dir_exp   = NULL;
float tolerance = 3.0;

int step         = 0;
int checks       = 0;
int failures     = 0;
int export_count = 0;

u8_array_t *island = NULL; // 1 for texels inside the UV islands, effects are fills and only cover those

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

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

// Mean of one channel of an exported texture, -1 when it was not exported
float channel_mean(char *dir, char *suffix, int channel) {
	float        mean  = -1.0;
	any_array_t *files = file_read_directory(dir);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *name = files->buffer[i];
		if (!ends_with(name, string("%s.png", suffix))) {
			continue;
		}
		script_import_asset(string("%s/%s", dir, name), false);
		gpu_texture_t *tex    = script_get_texture(name);
		buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
		if (pixels != NULL) {
			int   n      = pixels->length / 4;
			int   stride = n / 16384 + 1;
			float sum    = 0.0;
			int   count  = 0;
			for (int p = 0; p < n; p += stride) {
				if (island != NULL && (p >= island->length || island->buffer[p] == 0)) {
					continue;
				}
				sum += buffer_get_u8(pixels, p * 4 + channel);
				count += 1;
			}
			mean = count > 0 ? sum / count : -1.0;
		}
		script_texture_delete(name);
	}
	return mean;
}

// Exported base color, red and blue channels
void export_base(float *out) {
	export_count += 1;
	char *dir = string("%s/p1_effects%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	out[0] = channel_mean(dir, "_base", 0);
	out[1] = channel_mean(dir, "_base", 2);
}

void expect(float r, float b, char *when) {
	float v[2];
	export_base(v);
	check(absf(v[0] - r) <= tolerance && absf(v[1] - b) <= tolerance, string("%s: base %.0f %.0f (expected %.0f %.0f)", when, v[0], v[1], r, b));
}

// Levels with Out Black 1 and Out White 0 inverts its input
void set_invert(slot_layer_t *e) {
	script_material_set(e->fill_material);
	ui_node_t *n = script_material_get_node("LEVELS");
	script_material_set_float(n, 1, 4, 1.0);
	script_material_set_float(n, 1, 5, 0.0);
	script_material_update();
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	slot_layer_t *base = script_layer_new_color(0xff808080, 1.0, 0.5, 0.0);
	slot_layer_t *l    = script_layer_new_color(0xff3366cc, 1.0, 0.5, 0.0);
	script_layer_set_name(base, "fx_base");
	script_layer_set_name(l, "fx_layer");
}

// Texels the effect fill covered, from its alpha
void make_island(slot_layer_t *e) {
	buffer_t *pixels = script_get_texture_pixels(e->texpaint);
	int       n      = pixels->length / 4;
	int       inside = 0;
	island           = u8_array_create(n);
	for (int p = 0; p < n; ++p) {
		island->buffer[p] = buffer_get_u8(pixels, p * 4 + 3) > 250 ? 1 : 0;
		inside += island->buffer[p];
	}
	check(inside > n / 10 && inside < n, string("the effect covers the UV islands (%d of %d texels)", inside, n));
}

void step_content() {
	expect(51, 204, "no effect");
	slot_layer_t *l  = layer_by_name("fx_layer");
	slot_layer_t *e1 = script_layer_add_effect(l, EFFECT_TYPE_LEVELS);
	check(e1 != NULL && e1->parent == l && e1->fill_material != NULL && script_layer_index(e1) == script_layer_index(l) - 1,
	      "levels effect right below its layer");
	script_layer_set_name(e1, "fx_levels1");
	set_invert(e1);
}

void step_content_check() {
	make_island(layer_by_name("fx_levels1"));
	expect(51, 204, "no effect, inside the islands");
	script_layer_set_visible(layer_by_name("fx_levels1"), true);
	expect(204, 51, "levels inverts the layer");
}

void step_chain() {
	slot_layer_t *l  = layer_by_name("fx_layer");
	slot_layer_t *e2 = script_layer_add_effect(l, EFFECT_TYPE_LEVELS);
	script_layer_set_name(e2, "fx_levels2");
	check(e2 != NULL && script_layer_index(e2) == script_layer_index(layer_by_name("fx_levels1")) + 1, "second effect on top of the first");
	set_invert(e2);
	expect(51, 204, "two inverts give the layer back");
	script_layer_set_visible(layer_by_name("fx_levels1"), false);
	expect(204, 51, "the hidden first effect is skipped");
	script_layer_set_visible(layer_by_name("fx_levels2"), false);
	expect(51, 204, "both hidden");
	script_layer_set_visible(layer_by_name("fx_levels1"), true);
	expect(204, 51, "only the first");
	script_layer_set_visible(layer_by_name("fx_levels2"), true);
	expect(51, 204, "both shown again");
	check(script_layer_add_effect(layer_by_name("fx_layer"), EFFECT_TYPE_COMPARE_MASK) == NULL, "compare mask is only for masks");
}

void step_mask() {
	slot_layer_t *l = layer_by_name("fx_layer");
	slot_layer_t *m = script_layer_new_mask(l, 0);
	script_layer_set_name(m, "fx_mask");
	expect(128, 128, "black mask hides the layer");
	slot_layer_t *fill = script_layer_add_effect(m, EFFECT_TYPE_FILL);
	script_layer_set_name(fill, "fx_fill");
	check(fill != NULL && fill->effect && fill->parent == l && script_layer_index(fill) == script_layer_index(m) + 1, "fill effect right above its mask");
	expect(51, 204, "the fill effect starts white");
	slot_layer_t *lv = script_layer_add_effect(m, EFFECT_TYPE_LEVELS);
	script_layer_set_name(lv, "fx_mask_levels");
	check(lv != NULL && script_layer_index(lv) == script_layer_index(fill) + 1, "mask levels on top of the fill");
	set_invert(lv);
	expect(128, 128, "inverted fill hides the layer");
	script_layer_set_visible(layer_by_name("fx_fill"), false);
	expect(51, 204, "hidden fill: levels inverts the black mask");
	script_layer_set_visible(layer_by_name("fx_fill"), true);
	expect(128, 128, "fill shown again");
}

void step_undo() {
	int count = script_layer_count();
	script_undo(); // Visibility of fx_fill
	script_undo(); // Visibility of fx_fill
	script_undo(); // Mask levels, renames and material edits by script are not undo steps
	check(script_layer_count() == count - 1 && layer_by_name("fx_mask_levels") == NULL, "undo removes the mask levels");
}

void step_undo_check() {
	// Effects are refilled on the frame after undo
	expect(51, 204, "after undo the fill shows the layer");
	script_redo();
}

void step_redo_check() {
	slot_layer_t *lv = layer_by_name("Levels"); // Its name when it was created
	check(lv != NULL && lv->effect && lv->parent == layer_by_name("fx_layer"),
	      string("redo brings the mask levels back as an effect (%s)", lv != NULL ? lv->name : "none"));
	script_layer_set_name(lv, "fx_mask_levels");
}

void step_redo_effects() {
	expect(128, 128, "redo");
}

void step_save() {
	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	check(iron_file_exists(file_save), "project saved");
	script_project_new();
	script_project_open(file_save);
	slot_layer_t *fill = layer_by_name("fx_fill");
	check(fill != NULL && fill->effect && !layer_by_name("fx_mask")->effect && layer_by_name("fx_levels2") != NULL, "effects are saved");
	expect(128, 128, "reopened");
}

void step_delete() {
	int count = script_layer_count();
	script_layer_delete(layer_by_name("fx_layer"));
	check(script_layer_count() == count - 6 && layer_by_name("fx_levels1") == NULL && layer_by_name("fx_fill") == NULL,
	      string("deleting the layer deletes its effects and masks (%d layers)", script_layer_count()));
	expect(128, 128, "deleted");
}

// Copy and paste of an effect: the copy has its own material
void step_paste_effect() {
	slot_layer_t *a  = script_layer_new_color(0xff3366cc, 1.0, 0.5, 0.0);
	slot_layer_t *ea = script_layer_add_effect(a, EFFECT_TYPE_LEVELS);
	set_invert(ea);
	slot_layer_t *b = script_layer_new_color(0xff204080, 1.0, 0.5, 0.0); // On top, the composite shows it
	script_layer_set_name(b, "fx_paste");
	int count = script_layer_count();
	check(script_layer_paste(ea, b) && script_layer_count() == count + 1, "paste an effect into another layer");
	slot_layer_t *eb = script_layer_at(script_layer_index(layer_by_name("fx_paste")) - 1);
	check(eb != NULL && eb->parent == layer_by_name("fx_paste") && eb->fill_material != NULL && eb->fill_material != ea->fill_material,
	      "the pasted effect has a copy of the material");
	check(!script_layer_paste(ea, script_layer_new_mask(a, 1)), "a layer effect does not go into a mask");
}

void step_paste_effect_check() {
	expect(223, 127, "the pasted effect inverts the other layer");
}

void next_frame() {
	if (step == 0) {
		step_build();
	}
	else if (step == 1) {
		step_content();
	}
	else if (step == 2) {
		script_layer_set_visible(layer_by_name("fx_levels1"), false);
	}
	else if (step == 3) {
		step_content_check();
	}
	else if (step == 4) {
		step_chain();
	}
	else if (step == 5) {
		step_mask();
	}
	else if (step == 6) {
		step_undo();
	}
	else if (step == 7) {
		step_undo_check();
	}
	else if (step == 8) {
		step_redo_check();
	}
	else if (step == 9) {
		step_redo_effects();
	}
	else if (step == 10) {
		step_save();
	}
	else if (step == 11) {
		// project_save() runs on this frame
	}
	else if (step == 12) {
		step_reopen();
	}
	else if (step == 13) {
		step_delete();
	}
	else if (step == 14) {
		step_paste_effect();
	}
	else if (step == 15) {
		step_paste_effect_check();
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
	file_save = string("%s/p1_effects.arm", dir_out);
	dir_exp   = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	script_notify_on_next_frame(next_frame);
}
