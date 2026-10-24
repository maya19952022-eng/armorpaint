#include "global.h"

// BAKE-10: the Mesh maps export preset writes the baked mesh maps, gray ones as grayscale.
// Run with: base/tests/paint_headless/run.sh p2_mesh_map_export.c

char *dir_out = "../smoke/mesh_map_export";

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

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

char *file(char *name) {
	return string("%s/untitled_%s.png", dir_out, name);
}

// Red of the exported texel at the center of the default cube's front face island, 0-255
int red(char *name) {
	gpu_texture_t *t = data_get_texture(file(name));
	if (t == NULL) {
		return -1;
	}
	buffer_t *p = script_get_texture_pixels(t);
	if (p == NULL) {
		return -2;
	}
	int x = t->width / 2;
	int y = t->height / 2;
	return buffer_get_u8(p, (y * t->width + x) * 4);
}

void step_bake() {
	script_project_new();
	bake_settings_t *s = script_bake_get_settings();
	s->res             = 64;
	s->maps            = (1 << MESH_MAP_CURVATURE) | (1 << MESH_MAP_OPACITY) | (1 << MESH_MAP_WORLD_NORMAL);
	script_bake_selected();
}

void step_export() {
	check(script_export_set_preset("mesh_maps"), "mesh maps preset found");
	check(!script_export_set_preset("no_such_preset"), "a missing preset is refused");
	script_export_set_preset("mesh_maps");
	iron_create_directory("../smoke");
	iron_create_directory(dir_out);
	export_texture_run(dir_out, 0);
}

void check_files() {
	char *names[] = {"normal_base", "world_space_normals", "id", "ambient_occlusion", "curvature", "position", "thickness", "bent_normals", "opacity"};
	int   all     = 1;
	for (int i = 0; i < 9; ++i) {
		if (!iron_file_exists(file(names[i]))) {
			all = 0;
			printf("[smoke]   missing %s", file(names[i]));
		}
	}
	check(all, "a file per mesh map");
	check(red("opacity") == 255, string("baked opacity is white (%d)", red("opacity")));
	check(red("ambient_occlusion") == 255, string("unbaked ambient occlusion has its default, white (%d)", red("ambient_occlusion")));
	int curv = red("curvature");
	check(curv > 100 && curv < 155, string("flat faces have middle gray curvature (%d)", curv));
}

int waiting() {
	return step == 1 && script_bake_busy();
}

void next_frame() {
	if (waiting()) {
		script_notify_on_next_frame(next_frame);
		return;
	}
	if (step == 0) {
		step_bake();
	}
	else if (step == 1) {
		step_export();
	}
	else if (step == 2) {
		// Files are written
	}
	else if (step == 3) {
		check_files();
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
