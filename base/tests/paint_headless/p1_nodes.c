#include "global.h"

// Headless test of the P1 material nodes: Levels, Color Balance, Sharpen,
// Compare Mask and Color Selection (LAY-06, LAY-07, LAY-08, LAY-10).
// Each case builds a material, fills a new layer with it, exports the textures
// and compares the base color of the texels covered by the mesh UVs with a
// value computed by hand. Prints "SMOKE PASS" or "SMOKE FAIL".
// Run with: base/tests/paint_headless/run.sh p1_nodes.c
// Paths are relative to paint/build/out, the working directory of the app.

char *dir_out   = "../smoke";
char *dir_exp   = NULL;
float tolerance = 3.0; // Per 8-bit channel, the export maps v to about v * 255

int step     = 0;
int checks   = 0;
int failures = 0;
int cur      = 0; // Case index
int cases    = 17;

// Texels sampled from the exported base color, the first case keeps its samples
// to find the texels covered by the mesh UVs (background texels never change)
int       samples   = 0;
buffer_t *reference = NULL;
buffer_t *covered   = NULL;
int       covered_n = 0;

float exp_r = 0.0;
float exp_g = 0.0;
float exp_b = 0.0;
char *name  = "";

slot_layer_t *case_layer = NULL;

void check(int ok, char *what) {
	checks += 1;
	if (ok) {
		printf("[smoke]   ok   %s", what);
	}
	else {
		failures += 1;
		printf("[smoke]   FAIL %s", what);
	}
}

float absf(float f) {
	return f < 0.0 ? -f : f;
}

void clear_dir(char *dir) {
	any_array_t *old = file_read_directory(dir);
	for (int i = 0; old != NULL && i < old->length; ++i) {
		iron_delete_file(string("%s/%s", dir, old->buffer[i]));
	}
}

// Exports the textures and returns r, g, b samples of the base color
buffer_t *export_base() {
	char *dir = string("%s/case%d", dir_exp, cur);
	iron_create_directory(dir);
	clear_dir(dir);
	export_texture_run(dir, false);
	any_array_t *files = file_read_directory(dir);
	for (int i = 0; files != NULL && i < files->length; ++i) {
		char *file = files->buffer[i];
		if (!ends_with(file, "_base.png")) {
			continue;
		}
		script_import_asset(string("%s/%s", dir, file), false);
		gpu_texture_t *tex    = script_get_texture(file);
		buffer_t      *pixels = tex != NULL ? script_get_texture_pixels(tex) : NULL;
		buffer_t      *out    = NULL;
		if (pixels != NULL) {
			int n      = pixels->length / 4;
			int stride = n / 16384 + 1;
			samples    = 0;
			for (int p = 0; p < n; p += stride) {
				samples += 1;
			}
			out   = buffer_create(samples * 3);
			int s = 0;
			for (int p = 0; p < n; p += stride) {
				buffer_set_u8(out, s * 3, buffer_get_u8(pixels, p * 4));
				buffer_set_u8(out, s * 3 + 1, buffer_get_u8(pixels, p * 4 + 1));
				buffer_set_u8(out, s * 3 + 2, buffer_get_u8(pixels, p * 4 + 2));
				s += 1;
			}
		}
		script_texture_delete(file);
		return out;
	}
	return NULL;
}

// Mean over the covered texels
void covered_mean(buffer_t *b, float *m) {
	m[0]  = 0.0;
	m[1]  = 0.0;
	m[2]  = 0.0;
	int n = 0;
	for (int s = 0; s < samples; ++s) {
		if (buffer_get_u8(covered, s) == 0) {
			continue;
		}
		m[0] += buffer_get_u8(b, s * 3);
		m[1] += buffer_get_u8(b, s * 3 + 1);
		m[2] += buffer_get_u8(b, s * 3 + 2);
		n += 1;
	}
	if (n > 0) {
		m[0] = m[0] / n;
		m[1] = m[1] / n;
		m[2] = m[2] / n;
	}
}

void check_mean(buffer_t *b, char *what, float r, float g, float bl) {
	float m[3];
	covered_mean(b, m);
	r *= 255.0;
	g *= 255.0;
	bl *= 255.0;
	int ok = absf(m[0] - r) <= tolerance && absf(m[1] - g) <= tolerance && absf(m[2] - bl) <= tolerance;
	check(ok, string("%s: base %.1f %.1f %.1f (expected %.1f %.1f %.1f)", what, m[0], m[1], m[2], r, g, bl));
}

ui_node_t *output_node() {
	return script_material_get_node("OUTPUT_MATERIAL_PBR");
}

ui_node_t *new_node(char *type, float x) {
	ui_node_t *n = script_material_create_node_at(type, x, 0.0);
	if (n == NULL) {
		check(0, string("%s: node %s created", name, type));
	}
	return n;
}

ui_node_t *new_rgb(float r, float g, float b) {
	ui_node_t *n = new_node("RGB", -600.0);
	script_material_set_color(n, 0, 0, r, g, b, 1.0);
	return n;
}

void expect(float r, float g, float b) {
	exp_r = r;
	exp_g = g;
	exp_b = b;
}

// Material of a case connected to the base color, value outputs are gray
void build_case() {
	slot_material_t *m   = script_material_create(string("p1_case%d", cur));
	ui_node_t       *out = output_node();
	ui_node_t       *n   = NULL;
	ui_node_t       *src = NULL;

	if (cur == 0) {
		name = "pass-through RGB";
		n    = new_rgb(0.5, 0.25, 0.75);
		expect(0.5, 0.25, 0.75);
	}
	else if (cur == 1) {
		name = "pass-through RGB 2";
		n    = new_rgb(0.1, 0.9, 0.3);
		expect(0.1, 0.9, 0.3);
	}
	else if (cur == 2) {
		name = "Levels in_white 0.5 doubles a gray";
		src  = new_rgb(0.25, 0.25, 0.25);
		n    = new_node("LEVELS", -300.0);
		script_material_connect(src, 0, n, 0);
		script_material_set_float(n, 1, 2, 0.5); // In White
		expect(0.5, 0.5, 0.5);
	}
	else if (cur == 3) {
		// G only: pow(0.25, 1 / 2) = 0.5, 0.2 + (0.6 - 0.2) * 0.5 = 0.4
		name = "Levels channel G, gamma 2, out 0.2..0.6";
		src  = new_rgb(0.25, 0.25, 0.25);
		n    = new_node("LEVELS", -300.0);
		script_material_connect(src, 0, n, 0);
		script_material_set_float(n, 1, 3, 2.0); // Gamma
		script_material_set_float(n, 1, 4, 0.2); // Out Black
		script_material_set_float(n, 1, 5, 0.6); // Out White
		script_material_set_button(n, 0, 2);     // Channel G
		expect(0.25, 0.4, 0.25);
	}
	else if (cur == 4) {
		name = "Color Balance neutral is identity";
		src  = new_rgb(0.6, 0.3, 0.2);
		n    = new_node("COLOR_BALANCE", -300.0);
		script_material_connect(src, 0, n, 0);
		expect(0.6, 0.3, 0.2);
	}
	else if (cur == 5) {
		// l = 0.2126 * 0.6 + 0.7152 * 0.3 + 0.0722 * 0.2 = 0.35656
		// midtones weight 1 - (1 - l)^2 - l^2 = 0.45885, r = 0.6 + 0.1 * 0.45885
		name = "Color Balance midtones +0.1 red";
		src  = new_rgb(0.6, 0.3, 0.2);
		n    = new_node("COLOR_BALANCE", -300.0);
		script_material_connect(src, 0, n, 0);
		script_material_set_color(n, 1, 2, 0.6, 0.5, 0.5, 1.0); // Midtones
		script_material_set_button(n, 0, 0);                    // Preserve Luminosity off
		expect(0.645885, 0.3, 0.2);
	}
	else if (cur == 6) {
		// Scaled by l / dot(result, rec709) = 0.35656 / 0.366315 = 0.97337
		name = "Color Balance midtones +0.1 red, preserve luminosity";
		src  = new_rgb(0.6, 0.3, 0.2);
		n    = new_node("COLOR_BALANCE", -300.0);
		script_material_connect(src, 0, n, 0);
		script_material_set_color(n, 1, 2, 0.6, 0.5, 0.5, 1.0);
		expect(0.628685, 0.292011, 0.194674);
	}
	else if (cur == 7) {
		name = "Compare Mask 0.7 greater than 0.3";
		n    = new_node("COMPARE_MASK", -300.0);
		script_material_set_float(n, 1, 0, 0.7);
		script_material_set_float(n, 1, 1, 0.3);
		expect(1.0, 1.0, 1.0);
	}
	else if (cur == 8) {
		name = "Compare Mask 0.7 less than 0.3";
		n    = new_node("COMPARE_MASK", -300.0);
		script_material_set_float(n, 1, 0, 0.7);
		script_material_set_float(n, 1, 1, 0.3);
		script_material_set_button(n, 0, 1); // Less
		expect(0.0, 0.0, 0.0);
	}
	else if (cur == 9) {
		name = "Compare Mask 0.7 equal to 0.3 within 0.5";
		n    = new_node("COMPARE_MASK", -300.0);
		script_material_set_float(n, 1, 0, 0.7);
		script_material_set_float(n, 1, 1, 0.3);
		script_material_set_float(n, 1, 2, 0.5); // Threshold
		script_material_set_button(n, 0, 2);     // Equal
		expect(1.0, 1.0, 1.0);
	}
	else if (cur == 10) {
		name = "Compare Mask 0.7 not equal to 0.3 within 0.5";
		n    = new_node("COMPARE_MASK", -300.0);
		script_material_set_float(n, 1, 0, 0.7);
		script_material_set_float(n, 1, 1, 0.3);
		script_material_set_float(n, 1, 2, 0.5);
		script_material_set_button(n, 0, 3); // Not Equal
		expect(0.0, 0.0, 0.0);
	}
	else if (cur == 11) {
		// smoothstep(0, 0.1, 0.35 - 0.3) = 0.5
		name = "Compare Mask smoothness";
		n    = new_node("COMPARE_MASK", -300.0);
		script_material_set_float(n, 1, 0, 0.35);
		script_material_set_float(n, 1, 1, 0.3);
		script_material_set_float(n, 1, 3, 0.1); // Smoothness
		expect(0.5, 0.5, 0.5);
	}
	else if (cur == 12 || cur == 13 || cur == 14) {
		// Input red-ish, Color 1 blue, Color 2 the input color
		name = "Color Selection hit on the second color";
		if (cur == 13) {
			name = "Color Selection miss, count 1 skips the second color";
		}
		else if (cur == 14) {
			name = "Color Selection miss, inverted";
		}
		src = new_rgb(0.8, 0.2, 0.2);
		n   = new_node("COLOR_SELECTION", -300.0);
		script_material_connect(src, 0, n, 0);
		script_material_set_color(n, 1, 1, 0.0, 0.0, 1.0, 1.0);
		script_material_set_color(n, 1, 2, 0.8, 0.2, 0.2, 1.0);
		if (cur == 12) {
			script_material_set_button(n, 0, 2); // Count
			expect(1.0, 1.0, 1.0);
		}
		else if (cur == 13) {
			expect(0.0, 0.0, 0.0);
		}
		else {
			script_material_set_button(n, 1, 1); // Invert
			expect(1.0, 1.0, 1.0);
		}
	}
	else if (cur == 15) {
		name = "Sharpen keeps a flat color";
		src  = new_rgb(0.4, 0.5, 0.6);
		n    = new_node("SHARPEN", -300.0);
		script_material_connect(src, 0, n, 0);
		script_material_set_float(n, 1, 1, 2.0); // Amount
		script_material_set_float(n, 1, 2, 3.0); // Radius
		expect(0.4, 0.5, 0.6);
	}
	else if (cur == 16) {
		// Checker 0.2 / 0.8 in UV space, sharpening overshoots at the edges
		name           = "Sharpen overshoots at checker edges";
		ui_node_t *uv  = new_node("TEX_COORD", -900.0);
		ui_node_t *chk = new_node("TEX_CHECKER", -600.0);
		n              = new_node("SHARPEN", -300.0);
		script_material_connect(uv, 2, chk, 0);
		script_material_set_color(chk, 1, 1, 0.2, 0.2, 0.2, 1.0);
		script_material_set_color(chk, 1, 2, 0.8, 0.8, 0.8, 1.0);
		script_material_set_float(chk, 1, 3, 8.0);
		script_material_connect(chk, 0, n, 0);
		script_material_set_float(n, 1, 1, 1.0);
		script_material_set_float(n, 1, 2, 2.0);
		expect(-1.0, -1.0, -1.0);
	}

	if (n != NULL) {
		script_material_connect(n, 0, out, 0);
	}
	script_material_update(); // Bakes the sharpen input before the fill
	case_layer = script_layer_new_fill();
	check(case_layer != NULL, string("%s: fill layer created", name));
}

// Samples outside [51, 204] only appear when the edges are sharpened, not blurred or flipped
void check_sharpen_edges(buffer_t *b) {
	int below = 0;
	int above = 0;
	int flat  = 0;
	int total = 0;
	for (int s = 0; s < samples; ++s) {
		if (buffer_get_u8(covered, s) == 0) {
			continue;
		}
		int v = buffer_get_u8(b, s * 3 + 1);
		if (v < 41) {
			below += 1;
		}
		if (v > 214) {
			above += 1;
		}
		if (absf(v - 51.0) <= tolerance || absf(v - 204.0) <= tolerance) {
			flat += 1;
		}
		total += 1;
	}
	check(below > 0 && above > 0, string("%s: %d samples darker than 41, %d brighter than 214", name, below, above));
	check(flat * 2 > total, string("%s: %d of %d samples keep the checker colors", name, flat, total));
}

void run_case() {
	buffer_t *b = export_base();
	if (case_layer != NULL) {
		script_layer_delete(case_layer); // Only Layer 1 stays below the next case
		case_layer = NULL;
	}
	check(b != NULL, string("%s: base color exported", name));
	if (b == NULL) {
		return;
	}
	if (cur == 0) {
		reference = b;
		return;
	}
	if (cur == 1) {
		// Covered texels changed between the two pass-through colors
		covered   = buffer_create(samples);
		covered_n = 0;
		for (int s = 0; s < samples; ++s) {
			int d = buffer_get_u8(b, s * 3 + 1) - buffer_get_u8(reference, s * 3 + 1);
			buffer_set_u8(covered, s, d > 100 ? 1 : 0);
			covered_n += d > 100 ? 1 : 0;
		}
		check(covered_n * 4 > samples, string("covered texels: %d of %d samples", covered_n, samples));
		check_mean(reference, "pass-through RGB", 0.5, 0.25, 0.75);
	}
	if (cur == 16) {
		check_sharpen_edges(b);
		return;
	}
	check_mean(b, name, exp_r, exp_g, exp_b);
}

void next_frame() {
	if (step == 0) {
		check(script_get_config()->tools == TOOLS_PAINT, "paint mode (run.sh starts from the default config)");
		script_project_new();
	}
	else if (cur < cases) {
		int phase = (step - 1) % 3;
		if (phase == 0) {
			printf("[smoke] case %d", cur);
			build_case();
		}
		else if (phase == 2) {
			run_case();
			cur += 1;
		}
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
	dir_exp = string("%s/p1_nodes", dir_out);
	iron_create_directory(dir_out);
	iron_create_directory(dir_exp);
	printf("[smoke] output directory %s", dir_exp);
	script_notify_on_next_frame(next_frame);
}
