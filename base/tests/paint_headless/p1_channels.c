#include "global.h"

// LAY-01: blending and opacity per layer channel. Two color layers, the top one changes
// occlusion, roughness and metallic with its own blending and opacity while its base color
// channel is at zero opacity. Checks the exported textures, undo, save and reopen.
// Run with: base/tests/paint_headless/run.sh p1_channels.c

char *dir_out   = "../smoke";
char *file_save = NULL;
char *dir_exp   = NULL;

float tolerance = 3.0;

int step         = 0;
int sub          = 0; // Frame counter of a step that repeats
int step_again   = 0; // Set by a step to run again on the next frame
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

// Mean of the red channel of an exported texture, -1 when it was not exported
float channel_mean(char *dir, char *suffix) {
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
				sum += buffer_get_u8(pixels, p * 4);
				count += 1;
			}
			mean = sum / count;
		}
		script_texture_delete(name);
	}
	return mean;
}

char *export_now() {
	export_count += 1;
	char *dir = string("%s/p1_channels%d", dir_exp, export_count);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	return dir;
}

void expect(char *dir, char *suffix, float value, char *when) {
	float m = channel_mean(dir, suffix);
	check(absf(m - value) <= tolerance, string("%s: %s %.1f (expected %.1f)", when, suffix, m, value));
}

// Gray below, the top layer only reaches occlusion, roughness and metallic
void check_composite(char *when) {
	char *dir = export_now();
	expect(dir, "_base", 128.0, when);  // Base color channel at zero opacity
	expect(dir, "_occ", 25.5, when);    // Multiply: 0.5 * 0.2
	expect(dir, "_rough", 255.0, when); // Mix at full opacity
	expect(dir, "_metal", 127.5, when); // Mix at half opacity: 0.0 -> 1.0
}

void check_channels(slot_layer_t *top, char *when) {
	int ok = top != NULL && script_layer_get_channel_blending(top, LAYER_CH_BASE) == BLEND_TYPE_MIX &&
	         absf(script_layer_get_channel_opacity(top, LAYER_CH_BASE)) < 0.001 &&
	         script_layer_get_channel_blending(top, LAYER_CH_OCC) == BLEND_TYPE_MULTIPLY &&
	         absf(script_layer_get_channel_opacity(top, LAYER_CH_MET) - 0.5) < 0.001 &&
	         script_layer_get_channel_blending(top, LAYER_CH_NOR) == BLEND_TYPE_NORMAL_COMBINE &&
	         script_layer_get_channel_blending(top, LAYER_CH_HEIGHT) == BLEND_TYPE_ADD;
	check(ok, string("%s: channel blending and opacity", when));
}

// Mean color of a small square in the middle of a screenshot, taken over several frames.
// Returns 1 once the color is in vp, 0 while waiting for the screenshot.
float vp[3];

int viewport_color(char *name) {
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
	vp[0] = -1.0;
	vp[1] = -1.0;
	vp[2] = -1.0;
	script_import_asset(path, false);
	gpu_texture_t *tex    = script_get_texture(string("%s.png", name));
	buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
	if (pixels != NULL) {
		int   cx     = tex->width / 2;
		int   cy     = tex->height / 2;
		float sum[3] = {0.0, 0.0, 0.0};
		int   count  = 0;
		for (int y = cy - 4; y <= cy + 4; ++y) {
			for (int x = cx - 4; x <= cx + 4; ++x) {
				int p = (y * tex->width + x) * 4;
				sum[0] += buffer_get_u8(pixels, p);
				sum[1] += buffer_get_u8(pixels, p + 1);
				sum[2] += buffer_get_u8(pixels, p + 2);
				count += 1;
			}
		}
		vp[0] = sum[0] / count;
		vp[1] = sum[1] / count;
		vp[2] = sum[2] / count;
	}
	script_texture_delete(string("%s.png", name));
	printf("[smoke]        viewport %s %.1f %.1f %.1f", name, vp[0], vp[1], vp[2]);
	return 1;
}

float vp_base_off;  // Red of the base color view with the base channel at zero opacity
float vp_occ_mul;   // Occlusion view with multiply
float vp_rough_mix; // Roughness view

void step_viewport_base() {
	if (sub == 0) {
		context_set_viewport_mode(1); // VIEWPORT_MODE_BASE_COLOR
	}
	if (!viewport_color("p1_vp_base_off")) {
		step_again = 1;
		return;
	}
	vp_base_off = vp[0];
	check(vp[0] > 0.0 && absf(vp[0] - vp[2]) < 20.0, string("viewport: base channel at zero opacity stays gray (%.0f %.0f %.0f)", vp[0], vp[1], vp[2]));
}

void step_viewport_base_on() {
	if (sub == 0) {
		script_layer_set_channel(layer_by_name("p1_top"), LAYER_CH_BASE, BLEND_TYPE_MIX, 1.0);
	}
	if (!viewport_color("p1_vp_base_on")) {
		step_again = 1;
		return;
	}
	check(vp[0] > vp_base_off + 40.0 && vp[0] > vp[2] + 40.0, string("viewport: base channel at full opacity turns red (%.0f %.0f %.0f)", vp[0], vp[1], vp[2]));
	script_undo();
}

void step_viewport_occ() {
	if (sub == 0) {
		context_set_viewport_mode(3); // VIEWPORT_MODE_OCCLUSION
	}
	if (!viewport_color("p1_vp_occ_mul")) {
		step_again = 1;
		return;
	}
	vp_occ_mul = vp[0];
	check(vp[0] >= 0.0, "viewport: occlusion view");
}

void step_viewport_occ_mix() {
	if (sub == 0) {
		script_layer_set_channel(layer_by_name("p1_top"), LAYER_CH_OCC, BLEND_TYPE_MIX, 1.0);
	}
	if (!viewport_color("p1_vp_occ_mix")) {
		step_again = 1;
		return;
	}
	check(vp[0] > vp_occ_mul + 5.0, string("viewport: occlusion multiply (%.0f) is darker than mix (%.0f)", vp_occ_mul, vp[0]));
	script_undo();
}

void step_viewport_met() {
	if (sub == 0) {
		context_set_viewport_mode(5); // VIEWPORT_MODE_METALLIC
	}
	if (!viewport_color("p1_vp_met_half")) {
		step_again = 1;
		return;
	}
	vp_rough_mix = vp[0];
}

void step_viewport_met_full() {
	if (sub == 0) {
		script_layer_set_channel(layer_by_name("p1_top"), LAYER_CH_MET, BLEND_TYPE_MIX, 1.0);
	}
	if (!viewport_color("p1_vp_met_full")) {
		step_again = 1;
		return;
	}
	check(vp[0] > vp_rough_mix + 5.0, string("viewport: metallic at half opacity (%.0f) is below full opacity (%.0f)", vp_rough_mix, vp[0]));
	script_undo();
	context_set_viewport_mode(0); // VIEWPORT_MODE_LIT
}

void step_build() {
	check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
	script_project_new();
	slot_layer_t *gray = script_layer_new_color(0xff808080, 0.5, 0.5, 0.0);
	slot_layer_t *top  = script_layer_new_color(0xffff0000, 0.2, 1.0, 1.0);
	check(gray != NULL && top != NULL, "layers created");
	script_layer_set_name(top, "p1_top");

	// Defaults keep the old behaviour: normal combine, height add, the rest mix at full opacity
	int ok = script_layer_get_channel_blending(top, LAYER_CH_NOR) == BLEND_TYPE_NORMAL_COMBINE &&
	         script_layer_get_channel_blending(top, LAYER_CH_HEIGHT) == BLEND_TYPE_ADD &&
	         script_layer_get_channel_blending(top, LAYER_CH_ROUGH) == BLEND_TYPE_MIX && script_layer_get_channel_opacity(top, LAYER_CH_BASE) == 1.0;
	check(ok, "new layer channel defaults");

	script_layer_set_channel(top, LAYER_CH_BASE, BLEND_TYPE_MIX, 0.0);
	script_layer_set_channel(top, LAYER_CH_OCC, BLEND_TYPE_MULTIPLY, 1.0);
	script_layer_set_channel(top, LAYER_CH_ROUGH, BLEND_TYPE_MIX, 1.0);
	script_layer_set_channel(top, LAYER_CH_MET, BLEND_TYPE_MIX, 0.5);
	check(top->blending == BLEND_TYPE_MIX, "scripts read the base channel through blending");
	check_channels(top, "built");
}

void step_composite() {
	check_composite("built");
}

void step_normal_only() {
	// Changing the normal channel does not touch the other channels, undo brings it back
	slot_layer_t *top = layer_by_name("p1_top");
	script_layer_set_channel(top, LAYER_CH_NOR, BLEND_TYPE_MIX, 1.0);
	check(script_layer_get_channel_blending(top, LAYER_CH_NOR) == BLEND_TYPE_MIX, "normal channel set to mix");
	char *dir = export_now();
	expect(dir, "_base", 128.0, "normal mix");
	expect(dir, "_rough", 255.0, "normal mix");

	script_undo();
	top = layer_by_name("p1_top");
	check(top != NULL && script_layer_get_channel_blending(top, LAYER_CH_NOR) == BLEND_TYPE_NORMAL_COMBINE, "undo restores the normal channel");
	script_redo();
	top = layer_by_name("p1_top");
	check(top != NULL && script_layer_get_channel_blending(top, LAYER_CH_NOR) == BLEND_TYPE_MIX, "redo sets it again");
	script_undo();
}

void step_opacity_undo() {
	slot_layer_t *top = layer_by_name("p1_top");
	script_layer_set_channel(top, LAYER_CH_MET, BLEND_TYPE_MIX, 1.0);
	check(absf(script_layer_get_channel_opacity(top, LAYER_CH_MET) - 1.0) < 0.001, "metallic opacity set to 1");
	script_undo();
	top = layer_by_name("p1_top");
	check(top != NULL && absf(script_layer_get_channel_opacity(top, LAYER_CH_MET) - 0.5) < 0.001, "undo restores the metallic opacity");
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
	check(layer_by_name("p1_top") == NULL, "new project is empty");
	script_project_open(file_save);
	check_channels(layer_by_name("p1_top"), "reopened");
}

void step_composite_reopened() {
	check_composite("reopened");
}

void run_step(int st) {
	if (st == 0)
		step_build();
	else if (st == 1)
		step_composite();
	else if (st == 2)
		step_viewport_base();
	else if (st == 3)
		step_viewport_base_on();
	else if (st == 4)
		step_viewport_occ();
	else if (st == 5)
		step_viewport_occ_mix();
	else if (st == 6)
		step_viewport_met();
	else if (st == 7)
		step_viewport_met_full();
	else if (st == 8)
		step_composite(); // Undo brought every channel back
	else if (st == 9)
		step_normal_only();
	else if (st == 10)
		step_opacity_undo();
	else if (st == 11)
		step_save();
	else if (st == 12) {
		// project_save() runs on this frame
	}
	else if (st == 13)
		step_reopen();
	else if (st == 14)
		step_composite_reopened();
}

int step_count = 15;

void next_frame() {
	step_again = 0;
	run_step(step);
	if (step_again) {
		sub += 1;
	}
	else {
		sub = 0;
		step += 1;
	}
	if (step < step_count) {
		script_notify_on_next_frame(next_frame);
		return;
	}
	printf("[smoke] %d checks, %d failures", checks, failures);
	printf(failures == 0 && checks > 0 ? "SMOKE PASS" : "SMOKE FAIL");
	script_quit();
}

void main() {
	file_save = string("%s/p1_channels.arm", dir_out);
	dir_exp   = string("%s/export", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	script_notify_on_next_frame(next_frame);
}
