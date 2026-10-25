#include "global.h"

// LAY-02: groups are pass-through. A Multiply layer inside a group multiplies the layers below the
// group, the group opacity and the group mask apply to each layer of the group.
// Run with: base/tests/paint_headless/run.sh p1_groups.c

char *dir_out   = "../smoke";
char *dir_exp   = NULL;
float tolerance = 3.0;

int step         = 0;
int checks       = 0;
int failures     = 0;
int export_count = 0;

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
				sum += buffer_get_u8(pixels, p * 4 + channel);
				count += 1;
			}
			mean = sum / count;
		}
		script_texture_delete(name);
	}
	return mean;
}

void expect(float r, float g, char *when) {
	export_count += 1;
	char *dir = string("%s/p1_groups%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	float vr = channel_mean(dir, "_base", 0);
	float vg = channel_mean(dir, "_base", 1);
	check(absf(vr - r) <= tolerance && absf(vg - g) <= tolerance, string("%s: base %.0f %.0f (expected %.0f %.0f)", when, vr, vg, r, g));
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	slot_layer_t *base = script_layer_new_color(0xff808080, 1.0, 0.5, 0.0);
	slot_layer_t *red  = script_layer_new_color(0xffff0000, 1.0, 0.5, 0.0);
	script_layer_set_name(base, "g_base");
	script_layer_set_name(red, "g_red");
	script_layer_set_channel(red, LAYER_CH_BASE, BLEND_TYPE_MULTIPLY, 1.0);
	slot_layer_t *group = script_layer_new_group(red);
	script_layer_set_name(group, "g_group");
}

void step_pass_through() {
	// Multiply reaches the gray layer below the group: red 128, green 0. An isolated group would give pure red.
	expect(128, 0, "multiply inside a group multiplies the layers below it");
	slot_layer_t *group = layer_by_name("g_group");
	group->mask_opacity = 0.5;
	script_layer_update(layer_by_name("g_red"));
	// Half the multiply: green halfway between 128 and 0
	expect(128, 64, "group opacity applies to its layers");
	group->mask_opacity = 1.0;
	script_layer_new_mask(group, 0);
	script_layer_update(layer_by_name("g_red"));
	expect(128, 128, "a black group mask hides its layers");
}

void next_frame() {
	if (step == 0) {
		step_build();
	}
	else if (step == 1) {
		step_pass_through();
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
	dir_exp = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	script_notify_on_next_frame(next_frame);
}
