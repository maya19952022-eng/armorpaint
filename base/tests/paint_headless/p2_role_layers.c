#include "global.h"

// BAKE-06 and BAKE-07: the skew and cage map layers with the painting tools and the history.
// Run with: base/tests/paint_headless/run.sh p2_role_layers.c

int step     = 0;
int checks   = 0;
int failures = 0;

slot_layer_t *base = NULL;

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

slot_layer_t *find_role(int role) {
	for (int i = 0; i < script_layer_count(); ++i) {
		slot_layer_t *l = script_layer_at(i);
		if (l->bake_role == role) {
			return l;
		}
	}
	return NULL;
}

// Brightest gray of the texels the layer covers, and how many it covers
int max_gray(slot_layer_t *l) {
	buffer_t *p   = script_get_texture_pixels(l->texpaint);
	int       max = -1;
	for (int i = 0; i < p->length; i += 4) {
		if (buffer_get_u8(p, i + 3) > 0 && buffer_get_u8(p, i + 1) > max) {
			max = buffer_get_u8(p, i + 1);
		}
	}
	return max;
}

int covered(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	int       n = 0;
	for (int i = 0; i < p->length; i += 4) {
		if (buffer_get_u8(p, i + 3) > 0) {
			n += 1;
		}
	}
	return n;
}

int center_red(slot_layer_t *l) {
	buffer_t *p = script_get_texture_pixels(l->texpaint);
	int       w = l->texpaint->width;
	int       i = ((w / 2) * w + w / 2) * 4;
	return buffer_get_u8(p, i) > buffer_get_u8(p, i + 2) ? buffer_get_u8(p, i) : buffer_get_u8(p, i + 2); // Red in either byte order
}

void step_paint() {
	script_project_new();
	base               = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0); // Red
	slot_layer_t *skew = script_bake_paint_role(BAKE_ROLE_SKEW);
	check(covered(skew) == 0, "a new skew map is transparent");
	script_bake_get_settings()->paint_value = 1.0;
	context_select_tool(TOOL_TYPE_FILL);
	script_paint(0.5, 0.5);
	script_paint_end();
	int gray = max_gray(skew);
	check(covered(skew) > 0 && gray >= 250, string("the fill tool paints the value of the Baking tab, not the material gray (%d)", gray));
	script_bake_get_settings()->paint_value = 0.25;
	script_paint(0.5, 0.5);
	script_paint_end();
	gray = max_gray(skew);
	check(gray > 55 && gray < 75, string("a lower value paints a darker gray (%d)", gray));
	context_select_tool(TOOL_TYPE_BRUSH);
}

void step_fill_undo() {
	slot_layer_t *skew = find_role(BAKE_ROLE_SKEW);
	int           red  = center_red(base);
	script_layer_set(base);
	script_bake_role_fill(BAKE_ROLE_SKEW, 1.0);
	check(script_get_context()->layer == skew && max_gray(skew) >= 250, "fill selects the skew map and fills it");
	script_undo();
	check(max_gray(skew) < 75 && center_red(base) == red,
	      string("undo brings the painted gray back and leaves the other layer alone (%d %d)", max_gray(skew), center_red(base)));
	script_redo();
	check(max_gray(skew) >= 250 && center_red(base) == red, "redo fills the skew map again");
	script_bake_role_fill(BAKE_ROLE_SKEW, -1.0);
	check(covered(skew) == 0, "clear makes it transparent");
	script_undo();
	check(covered(skew) > 0, "undo of clear");
}

void step_delete_undo() {
	slot_layer_t *skew = find_role(BAKE_ROLE_SKEW);
	script_layer_delete(skew);
	check(find_role(BAKE_ROLE_SKEW) == NULL, "skew map deleted");
	script_undo();
	slot_layer_t *back = find_role(BAKE_ROLE_SKEW);
	check(back != NULL, "undo of delete brings the skew map back with its role");
	script_redo();
	check(find_role(BAKE_ROLE_SKEW) == NULL, "redo deletes it again");
	script_undo();

	// New layer undo and redo
	slot_layer_t *cage = script_bake_paint_role(BAKE_ROLE_CAGE);
	check(cage != NULL && find_role(BAKE_ROLE_CAGE) != NULL, "cage map made");
	script_undo();
	check(find_role(BAKE_ROLE_CAGE) == NULL, "undo removes the new cage map");
	script_redo();
	check(find_role(BAKE_ROLE_CAGE) != NULL, "redo makes it again as a cage map");
}

void next_frame() {
	if (step == 0) {
		step_paint();
	}
	else if (step == 1) {
		step_fill_undo();
	}
	else if (step == 2) {
		step_delete_undo();
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
