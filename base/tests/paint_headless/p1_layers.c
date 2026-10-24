#include "global.h"

// LAY-03/04/15/16: layer lock, paste and paste as mask, fill masks that start white and
// multi selection (select, move, group, flatten and delete in batch), with undo.
// Run with: base/tests/paint_headless/run.sh p1_layers.c

char *dir_out   = "../smoke";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;

float red_before = 0.0;

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

// Mean of the red channel of a layer or mask texture, -1 when it can not be read. Render targets are
// BGRA on some backends, the larger of bytes 0 and 2 is red for masks (only red is written) and for
// the layers here, whose colors have red equal to blue. With lit, pixels below 8 are left out.
float mean_of(slot_layer_t *l, int lit) {
	if (l == NULL || l->texpaint == NULL) {
		return -1.0;
	}
	buffer_t *pixels = script_get_texture_pixels(l->texpaint);
	if (pixels == NULL) {
		return -1.0;
	}
	int   n      = pixels->length / 4;
	int   stride = n / 16384 + 1;
	float sum    = 0.0;
	int   count  = 0;
	for (int p = 0; p < n; p += stride) {
		int b0 = buffer_get_u8(pixels, p * 4);
		int b2 = buffer_get_u8(pixels, p * 4 + 2);
		int v  = b0 > b2 ? b0 : b2;
		if (lit && v < 8) {
			continue;
		}
		sum += v;
		count += 1;
	}
	return count > 0 ? sum / count : 0.0;
}

float red_mean(slot_layer_t *l) {
	return mean_of(l, 0);
}

// Layer names from the bottom up, separated by spaces
char *order() {
	char *s = "";
	for (int i = 0; i < script_layer_count(); ++i) {
		s = string("%s%s%s", s, i > 0 ? " " : "", script_layer_at(i)->name);
	}
	return s;
}

void stroke() {
	script_paint(0.35, 0.5);
	script_paint(0.5, 0.5);
	script_paint(0.65, 0.5);
	script_paint_end();
}

slot_layer_t *new_layer(int color, char *name) {
	slot_layer_t *l = script_layer_new_color(color, 1.0, 0.5, 0.0);
	script_layer_set_name(l, name);
	return l;
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	new_layer(0xff808080, "p1_base");
	new_layer(0xffff00ff, "p1_red"); // Magenta, red equal to blue
	red_before = red_mean(layer_by_name("p1_red"));
	check(red_before > 250.0, string("red layer reads back as red (%.0f)", red_before));
}

void step_lock() {
	slot_layer_t *red = layer_by_name("p1_red");
	script_layer_set_locked(red, true);
	check(red->locked, "layer locked");
	script_layer_set(red);
	stroke();
	check(absf(red_mean(red) - red_before) < 0.5, string("painting a locked layer changes nothing (%.1f)", red_mean(red)));
	script_undo();
	check(!red->locked, "undo unlocks");
	script_redo();
	check(red->locked, "redo locks again");
	script_layer_set_locked(red, false);
	stroke();
	check(red_mean(red) < red_before - 0.5, string("painting the unlocked layer changes it (%.1f)", red_mean(red)));
	script_undo();
	check(absf(red_mean(red) - red_before) < 0.5, string("undo of the stroke (%.1f)", red_mean(red)));
	script_layer_set_locked(red, true);
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
	slot_layer_t *red  = layer_by_name("p1_red");
	slot_layer_t *base = layer_by_name("p1_base");
	check(red != NULL && red->locked && base != NULL && !base->locked, "lock is saved in the project");
	script_layer_set_locked(red, false);
}

void step_paste() {
	slot_layer_t *red   = layer_by_name("p1_red");
	slot_layer_t *base  = layer_by_name("p1_base");
	int           count = script_layer_count();
	check(script_layer_paste(red, base), "paste a layer");
	slot_layer_t *copy = script_layer_at(script_layer_index(base) + 1);
	check(script_layer_count() == count + 1 && copy != red && copy != NULL, "the copy is a new layer right above the target");
	check(copy != NULL && absf(red_mean(copy) - red_before) < 0.5, string("the copy has the pixels of the source (%.0f)", red_mean(copy)));
	script_undo();
	script_undo();
	check(script_layer_count() == count && string_equals(order(), "Layer 1 p1_base p1_red"), string("undo removes the copy (%s)", order()));
}

void step_paste_mask() {
	slot_layer_t *red  = layer_by_name("p1_red");
	slot_layer_t *base = layer_by_name("p1_base");
	slot_layer_t *src  = script_layer_new_mask(red, 0);
	src->mask_opacity  = 0.4;
	int           count = script_layer_count();
	slot_layer_t *m     = script_layer_paste_mask(src, base);
	check(m != NULL && m->parent == base && script_layer_count() == count + 1, "paste as mask adds a mask to the target");
	check(m != NULL && red_mean(m) < 1.0 && absf(m->mask_opacity - 0.4) < 0.001,
	      string("the new mask has the pixels and opacity of the source (%.0f)", m != NULL ? red_mean(m) : -1.0));
	script_undo();
	check(script_layer_count() == count + 1 && red_mean(m) > 254.0, string("undo brings back the white mask (%.0f)", red_mean(m)));
	script_undo();
	check(script_layer_count() == count, "undo again removes the mask");
	check(script_layer_paste_mask(red, base) == NULL, "a layer can not be pasted as a mask");
}

void step_fill_mask() {
	slot_layer_t *base  = layer_by_name("p1_base");
	int           count = script_layer_count();
	slot_layer_t *m     = script_layer_new_mask(base, 3);
	check(m != NULL && m->fill_material != NULL && script_layer_count() == count + 1, "new fill mask");
	check(m != NULL && mean_of(m, 1) > 250.0, string("the fill mask starts white (%.0f where it covers the mesh)", m != NULL ? mean_of(m, 1) : -1.0));
	check(m != NULL && m->fill_material != NULL && string_index_of(m->fill_material->canvas->name, "p1_base") == 0,
	      string("with its own material (%s)", m != NULL && m->fill_material != NULL ? m->fill_material->canvas->name : ""));
	script_undo();
	check(script_layer_count() == count, "undo removes the fill mask");
	script_undo(); // The material
}

void step_select() {
	script_project_new();
	new_layer(0xff202020, "s1");
	new_layer(0xff404040, "s2");
	new_layer(0xff606060, "s3");
	new_layer(0xff808080, "s4");
	check(string_equals(order(), "Layer 1 s1 s2 s3 s4"), order());
	script_layer_select(layer_by_name("s1"), false, false);
	check(script_layer_selected_count() == 1, "a plain click selects one layer");
	script_layer_select(layer_by_name("s3"), true, false);
	check(script_layer_selected_count() == 2, "ctrl+click adds a layer");
	script_layer_select(layer_by_name("s2"), true, false);
	script_layer_select(layer_by_name("s1"), true, false);
	check(script_layer_selected_count() == 2, "ctrl+click again removes it");
	script_layer_select(layer_by_name("s1"), false, false);
	script_layer_select(layer_by_name("s4"), false, true);
	check(script_layer_selected_count() == 4, string("shift+click selects the range (%d)", script_layer_selected_count()));
	script_layer_select(layer_by_name("s2"), false, false);
	check(script_layer_selected_count() == 1, "a plain click clears the selection");
}

void step_move() {
	script_layer_select(layer_by_name("s1"), false, false);
	script_layer_select(layer_by_name("s2"), true, false);
	script_layer_move_selected(script_layer_index(layer_by_name("s4")));
	check(string_equals(order(), "Layer 1 s3 s4 s1 s2"), string("moving up keeps the order (%s)", order()));
	script_undo();
	check(string_equals(order(), "Layer 1 s1 s2 s3 s4"), string("one undo step moves them all back (%s)", order()));
	script_redo();
	check(string_equals(order(), "Layer 1 s3 s4 s1 s2"), string("redo (%s)", order()));
	script_layer_move_selected(1);
	check(string_equals(order(), "Layer 1 s1 s2 s3 s4"), string("moving down keeps the order (%s)", order()));
	check(script_layer_selected_count() == 2, "the selection stays after the move");
	// A single layer dragged over several others, the old undo only swapped two layers
	script_layer_select(layer_by_name("s4"), false, false);
	script_layer_move_selected(1);
	check(string_equals(order(), "Layer 1 s4 s1 s2 s3"), string("move one layer down three places (%s)", order()));
	script_undo();
	check(string_equals(order(), "Layer 1 s1 s2 s3 s4"), string("undo puts it back (%s)", order()));
}

void step_group() {
	script_layer_select(layer_by_name("s1"), false, false);
	script_layer_select(layer_by_name("s3"), true, false);
	slot_layer_t *group = script_layer_group_selected();
	slot_layer_t *s1    = layer_by_name("s1");
	slot_layer_t *s3    = layer_by_name("s3");
	check(group != NULL && s1->parent == group && s3->parent == group, "group selected layers");
	check(group != NULL && script_layer_index(s1) < script_layer_index(s3) && script_layer_index(s3) + 1 == script_layer_index(group),
	      string("the group keeps their order (%s)", order()));
	check(layer_by_name("s2")->parent == NULL && layer_by_name("s4")->parent == NULL, "the other layers stay out");
	int undo = 0;
	while (layer_by_name("s3")->parent != NULL && undo < 4) {
		script_undo();
		undo += 1;
	}
	check(layer_by_name("s3")->parent == NULL && string_equals(order(), "Layer 1 s1 s2 s3 s4"), string("undo ungroups (%d steps, %s)", undo, order()));
}

void step_delete() {
	int count = script_layer_count();
	script_layer_select(layer_by_name("s2"), false, false);
	script_layer_select(layer_by_name("s4"), true, false);
	script_layer_delete_selected();
	check(script_layer_count() == count - 2 && layer_by_name("s2") == NULL && layer_by_name("s4") == NULL, string("delete selected (%s)", order()));
	script_undo();
	script_undo();
	check(script_layer_count() == count && layer_by_name("s2") != NULL && layer_by_name("s4") != NULL, string("undo brings them back (%s)", order()));
}

void step_flatten() {
	script_layer_select(layer_by_name("s2"), false, false);
	script_layer_select(layer_by_name("s4"), true, false);
	int           count = script_layer_count();
	slot_layer_t *flat  = script_layer_flatten_selected();
	slot_layer_t *s2    = layer_by_name("s2");
	check(flat != NULL && script_layer_count() == count + 2, string("flatten selected adds a group and a layer (%s)", order()));
	check(flat != NULL && s2->parent != NULL && !s2->parent->visible && layer_by_name("s4")->parent == s2->parent,
	      "the selected layers are kept in a hidden group");
	check(flat != NULL && absf(red_mean(flat) - 128.0) < 3.0, string("the flattened layer shows the top one (%.0f)", red_mean(flat)));
}

// Undo of Merge Down rebuilds both layers, each with its own name, channels and opacity
void step_merge_undo() {
	script_project_new();
	slot_layer_t *g = new_layer(0xff808080, "m_lower");
	slot_layer_t *t = new_layer(0xffff00ff, "m_upper");
	script_layer_set_channel(g, LAYER_CH_OCC, BLEND_TYPE_MULTIPLY, 0.5);
	script_layer_set_channel(g, LAYER_CH_NOR, BLEND_TYPE_MIX, 0.25);
	g->mask_opacity = 0.7;
	script_layer_set_channel(t, LAYER_CH_BASE, BLEND_TYPE_SCREEN, 0.75);
	check(script_layer_merge_down(t) != NULL && layer_by_name("m_upper") == NULL, "merge down");
	script_undo();
	g = layer_by_name("m_lower");
	t = layer_by_name("m_upper");
	check(g != NULL && t != NULL && script_layer_index(g) + 1 == script_layer_index(t), string("undo brings back both layers by name (%s)", order()));
	check(g != NULL && script_layer_get_channel_blending(g, LAYER_CH_OCC) == BLEND_TYPE_MULTIPLY &&
	          absf(script_layer_get_channel_opacity(g, LAYER_CH_OCC) - 0.5) < 0.001 &&
	          script_layer_get_channel_blending(g, LAYER_CH_NOR) == BLEND_TYPE_MIX && absf(g->mask_opacity - 0.7) < 0.001,
	      "the lower layer keeps its channels and opacity");
	check(t != NULL && script_layer_get_channel_blending(t, LAYER_CH_BASE) == BLEND_TYPE_SCREEN &&
	          absf(script_layer_get_channel_opacity(t, LAYER_CH_BASE) - 0.75) < 0.001,
	      "the upper layer keeps its channels");
}

void next_frame() {
	if (step == 0) {
		step_build();
	}
	else if (step == 1) {
		step_lock();
	}
	else if (step == 2) {
		step_save();
	}
	else if (step == 3) {
		// project_save() runs on this frame
	}
	else if (step == 4) {
		step_reopen();
	}
	else if (step == 5) {
		step_paste();
	}
	else if (step == 6) {
		step_paste_mask();
	}
	else if (step == 7) {
		step_fill_mask();
	}
	else if (step == 8) {
		step_select();
	}
	else if (step == 9) {
		step_move();
	}
	else if (step == 10) {
		step_group();
	}
	else if (step == 11) {
		step_delete();
	}
	else if (step == 12) {
		step_flatten();
	}
	else if (step == 13) {
		step_merge_undo();
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
	file_save = string("%s/p1_layers.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
