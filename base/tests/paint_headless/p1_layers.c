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

// Fixes from review: adjacent layers in Group Selected, a drop inside the selection, a move that empties a group,
// Merge Down in a group with a group mask, the selection after a new layer, group lock, paste as mask redo
void step_review_group() {
	script_project_new();
	new_layer(0xff202020, "r1");
	new_layer(0xff404040, "r2");
	new_layer(0xff606060, "r3");
	script_layer_select(layer_by_name("r1"), false, false);
	script_layer_select(layer_by_name("r3"), false, true);
	slot_layer_t *g = script_layer_group_selected();
	check(g != NULL && layer_by_name("r1")->parent == g && layer_by_name("r2")->parent == g && layer_by_name("r3")->parent == g &&
	          string_equals(order(), string("Layer 1 r1 r2 r3 %s", g->name)),
	      string("group a contiguous range keeps every layer and the order (%s)", order()));
	script_layer_set_name(g, "rg");
	// Move the three out of the group to the top: the empty group is removed, undo brings it back
	script_layer_select(layer_by_name("r1"), false, false);
	script_layer_select(layer_by_name("r3"), false, true);
	script_layer_move_selected(script_layer_count() - 1);
	check(layer_by_name("rg") == NULL && layer_by_name("r1")->parent == NULL, string("moving every layer out removes the group (%s)", order()));
	script_undo();
	slot_layer_t *rg = layer_by_name("rg");
	check(rg != NULL && layer_by_name("r1")->parent == rg && layer_by_name("r3")->parent == rg && string_equals(order(), "Layer 1 r1 r2 r3 rg"),
	      string("undo brings the group back (%s)", order()));
	script_redo();
	check(layer_by_name("rg") == NULL && script_layer_count() == 4, string("redo (%s)", order()));
	script_undo();
}

void step_review_drop() {
	script_project_new();
	new_layer(0xff202020, "d1");
	new_layer(0xff404040, "d2");
	new_layer(0xff606060, "d3");
	new_layer(0xff808080, "d4");
	script_layer_new_mask(layer_by_name("d1"), 1);
	script_layer_set_name(script_layer_at(script_layer_index(layer_by_name("d1")) - 1), "d1m");
	script_layer_select(layer_by_name("d1"), false, false);
	script_layer_select(layer_by_name("d4"), true, false);
	check(script_layer_selected_count() == 2, "d1 and d4 selected");
	script_layer_move_selected(script_layer_index(layer_by_name("d2")));
	check(string_equals(order(), "Layer 1 d1m d1 d4 d2 d3") || string_equals(order(), "Layer 1 d2 d1m d1 d4 d3"),
	      string("dropping inside the selection keeps its order and the mask with its layer (%s)", order()));
	check(layer_by_name("d1m")->parent == layer_by_name("d1"), "the mask stays on its layer");
	script_undo();
	check(string_equals(order(), "Layer 1 d1m d1 d2 d3 d4"), string("undo (%s)", order()));
	new_layer(0xffa0a0a0, "d5");
	check(script_layer_selected_count() == 1, string("a new layer ends the multi selection (%d)", script_layer_selected_count()));
}

void step_review_merge() {
	script_project_new();
	new_layer(0xff202020, "ma");
	slot_layer_t *g  = script_layer_new_group(layer_by_name("ma"));
	slot_layer_t *gm = script_layer_new_mask(g, 1);
	check(g != NULL && gm != NULL && gm->parent == g, string("group with a group mask (%s)", order()));
	slot_layer_t *mb = new_layer(0xff404040, "mb");
	script_layer_set_group(mb, g);
	int count = script_layer_count();
	slot_layer_t *merged = script_layer_merge_down(layer_by_name("mb"));
	check(merged != NULL, string("merge down inside a group with a group mask (%s)", order()));
	script_undo();
	check(layer_by_name("ma") != NULL && layer_by_name("mb") != NULL, string("undo of the merge (%s, %d layers, %d before)", order(), script_layer_count(), count));
}

// Group lock reaches the layers of the group
void step_review_group_lock() {
	script_project_new();
	slot_layer_t *la = new_layer(0xffff00ff, "la");
	slot_layer_t *g  = script_layer_new_group(la);
	script_layer_set_locked(g, true);
	script_layer_set(la);
	float before = red_mean(la);
	stroke();
	check(g != NULL && la->parent == g && absf(red_mean(la) - before) < 0.5,
	      string("a locked group blocks painting its layers (%.1f %.1f)", before, red_mean(la)));
	script_layer_set_locked(g, false);
	script_layer_set(la);
	stroke();
	check(red_mean(la) < before - 0.5, string("unlocked again (%.1f)", red_mean(la)));
}

void step_review_paste_mask() {
	script_project_new();
	slot_layer_t *a = new_layer(0xff808080, "pa");
	slot_layer_t *b = new_layer(0xff808080, "pb");
	slot_layer_t *m = script_layer_new_mask(a, 0);
	m->mask_opacity = 0.4;
	slot_layer_t *pm = script_layer_paste_mask(m, b);
	check(pm != NULL && absf(pm->mask_opacity - 0.4) < 0.001, "pasted mask opacity");
	script_undo();
	script_undo();
	script_redo();
	script_redo();
	slot_layer_t *again = script_layer_at(script_layer_index(layer_by_name("pb")) - 1);
	check(again != NULL && again->parent == layer_by_name("pb") && absf(again->mask_opacity - 0.4) < 0.001,
	      string("redo keeps the opacity of the pasted mask (%.2f)", again != NULL ? again->mask_opacity : -1.0));
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
	else if (step == 14) {
		step_review_group();
	}
	else if (step == 15) {
		step_review_drop();
	}
	else if (step == 16) {
		step_review_merge();
	}
	else if (step == 17) {
		step_review_paste_mask();
	}
	else if (step == 18) {
		step_review_group_lock();
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
