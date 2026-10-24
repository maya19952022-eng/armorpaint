#include "global.h"

// LAY-14: non destructive group flatten. A group with two layers over a gray layer is flattened:
// the composite stays the same, the group is kept hidden with its layers, undo and redo work,
// the result matches Merge Group, and the group saves as an .arm holding only its layers.
// Run with: base/tests/paint_headless/run.sh p1_flatten.c

char *dir_out   = "../smoke";
char *file_arm  = NULL;
char *dir_exp   = NULL;
float tolerance = 3.0;

int step         = 0;
int checks       = 0;
int failures     = 0;
int export_count = 0;

float composite[4]; // base r, g, b and roughness of the unflattened project

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

int children_of(slot_layer_t *group) {
	int count = 0;
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->parent == group) {
			count += 1;
		}
	}
	return count;
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
				sum += buffer_get_u8(pixels, p * 4 + channel);
				count += 1;
			}
			mean = sum / count;
		}
		script_texture_delete(name);
	}
	return mean;
}

void export_values(float *out) {
	export_count += 1;
	char *dir = string("%s/p1_flatten%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	out[0] = channel_mean(dir, "_base", 0);
	out[1] = channel_mean(dir, "_base", 1);
	out[2] = channel_mean(dir, "_base", 2);
	out[3] = channel_mean(dir, "_rough", 0);
}

void check_composite(char *when) {
	float v[4];
	export_values(v);
	int ok = absf(v[0] - composite[0]) <= tolerance && absf(v[1] - composite[1]) <= tolerance && absf(v[2] - composite[2]) <= tolerance &&
	         absf(v[3] - composite[3]) <= tolerance;
	check(ok, string("%s: composite %.0f %.0f %.0f rough %.0f (expected %.0f %.0f %.0f rough %.0f)", when, v[0], v[1], v[2], v[3], composite[0], composite[1],
	                 composite[2], composite[3]));
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	script_layer_new_color(0xff808080, 1.0, 0.5, 0.0);
	slot_layer_t *red   = script_layer_new_color(0xffff0000, 1.0, 0.2, 0.0);
	slot_layer_t *group = script_layer_new_group(red);
	slot_layer_t *blue  = script_layer_new_color(0xff0000ff, 1.0, 0.9, 0.0);
	script_layer_set_name(group, "p1_group");
	script_layer_set_name(red, "p1_red");
	script_layer_set_name(blue, "p1_blue");
	int ok = group != NULL && script_layer_set_group(blue, group) && children_of(group) == 2;
	check(ok, "group with two layers");
	blue->mask_opacity = 0.5;
	script_layer_set_channel(blue, LAYER_CH_BASE, BLEND_TYPE_ADD, 1.0);
	script_layer_update(blue);
}

void step_composite() {
	export_values(composite);
	check(composite[0] > 200.0 && composite[2] > 100.0,
	      string("group composite is red plus half blue (%.0f %.0f %.0f)", composite[0], composite[1], composite[2]));
}

void step_flatten() {
	slot_layer_t *group = layer_by_name("p1_group");
	int           count = script_layer_count();
	slot_layer_t *flat  = script_layer_flatten_group(group);
	check(flat != NULL && flat->parent == NULL && script_layer_count() == count + 1, "flatten adds one layer");
	check(flat != NULL && string_index_of(flat->name, "p1_group") == 0,
	      string("flattened layer is named after the group (%s)", flat != NULL ? flat->name : ""));
	group = layer_by_name("p1_group");
	check(group != NULL && !group->visible && children_of(group) == 2 && layer_by_name("p1_red") != NULL && layer_by_name("p1_blue") != NULL,
	      "group is kept hidden with its layers");
	check(flat != NULL && script_layer_index(flat) == script_layer_index(group) + 1, "flattened layer is right above the group");
	check_composite("flattened");
}

void step_undo() {
	int count = script_layer_count();
	script_undo();
	slot_layer_t *group = layer_by_name("p1_group");
	check(script_layer_count() == count - 1 && group != NULL && group->visible, "undo removes the flattened layer and shows the group");
	check_composite("undo");
	script_redo();
	group = layer_by_name("p1_group");
	check(script_layer_count() == count && group != NULL && !group->visible, "redo flattens again");
	check_composite("redo");
	script_undo();
}

void step_merge_group() {
	// The same composite through the destructive Merge Group, last as its undo does not bring the group back
	slot_layer_t *merged = script_layer_merge_group(layer_by_name("p1_group"));
	check(merged != NULL && layer_by_name("p1_red") == NULL, "merge group");
	check_composite("merge group");
}

void step_export_arm() {
	slot_layer_t *group = layer_by_name("p1_group");
	if (iron_file_exists(file_arm)) {
		iron_delete_file(file_arm);
	}
	check(group != NULL && script_layer_export_arm(group, file_arm) && iron_file_exists(file_arm), "group saved as .arm");
}

void step_open_arm() {
	script_project_open(file_arm);
	slot_layer_t *group = layer_by_name("p1_group");
	slot_layer_t *red   = layer_by_name("p1_red");
	slot_layer_t *blue  = layer_by_name("p1_blue");
	int           ok    = script_layer_count() == 3 && group != NULL && red != NULL && blue != NULL && red->parent == group && blue->parent == group &&
	         script_layer_get_channel_blending(blue, LAYER_CH_BASE) == BLEND_TYPE_ADD && absf(blue->mask_opacity - 0.5) < 0.001;
	check(ok, string("the .arm holds only the group and its layers (%d layers)", script_layer_count()));
}

void next_frame() {
	if (step == 0) {
		step_build();
	}
	else if (step == 1) {
		step_composite();
	}
	else if (step == 2) {
		step_flatten();
	}
	else if (step == 3) {
		step_undo();
	}
	else if (step == 4) {
		step_export_arm();
	}
	else if (step == 5) {
		step_merge_group();
	}
	else if (step == 6) {
		step_open_arm();
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
	file_arm = string("%s/p1_group.arm", dir_out);
	dir_exp  = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	script_notify_on_next_frame(next_frame);
}
