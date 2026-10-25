#include "global.h"

// BAKE-02: the maps checked in the Baking tab, Bake Selected, settings saved with the project.
// Run with: base/tests/paint_headless/run.sh p2_baking.c

char *dir_out   = "../smoke";
char *file_save = NULL;

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

int only_baked(int maps) {
	for (int i = 0; i < MESH_MAP_COUNT; ++i) {
		int want = (maps >> i) & 1;
		int has  = script_mesh_map_get(i) != NULL;
		if (want != has) {
			return 0;
		}
	}
	return 1;
}

void step_select() {
	script_project_new();
	bake_settings_t *s = script_bake_get_settings();
	check(s->maps == (1 << MESH_MAP_COUNT) - 1, "every map is selected by default");
	s->res               = 32;
	s->dilation          = 1;
	s->occlusion_samples = 8;
	s->maps              = (1 << MESH_MAP_OCCLUSION) | (1 << MESH_MAP_CURVATURE);
	script_show_tab(TAB_BAKING);
	script_bake_selected();
	check(script_bake_busy(), "Bake Selected queues the checked maps");
}

void check_selected() {
	check(only_baked((1 << MESH_MAP_OCCLUSION) | (1 << MESH_MAP_CURVATURE)), "only the checked maps are baked");
	bake_settings_t *s = script_bake_get_settings();
	s->maps            = 0;
	script_bake_selected();
	check(!script_bake_busy(), "nothing checked, nothing queued");
	s->maps = 1 << MESH_MAP_POSITION;
	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	script_project_new();
	script_project_open(file_save);
	bake_settings_t *s = script_bake_get_settings();
	check(s->maps == (1 << MESH_MAP_POSITION), "the checked maps survive save and load");
	check(s->res == 32 && s->dilation == 1 && s->occlusion_samples == 8, "common and per map settings survive save and load");
	check(only_baked((1 << MESH_MAP_OCCLUSION) | (1 << MESH_MAP_CURVATURE)), "baked maps survive save and load");
	script_bake_selected();
}

void check_reopened() {
	check(only_baked((1 << MESH_MAP_OCCLUSION) | (1 << MESH_MAP_CURVATURE) | (1 << MESH_MAP_POSITION)), "Bake Selected after opening adds the checked map");
}

int waits_for_bake(int s) {
	return s == 1 || s == 4;
}

void next_frame() {
	if (waits_for_bake(step) && script_bake_busy()) {
		waited += 1;
		if (waited > 3000) {
			check(0, "bake finished in time");
			script_bake_cancel();
		}
		script_notify_on_next_frame(next_frame);
		return;
	}
	waited = 0;

	if (step == 0) {
		step_select();
	}
	else if (step == 1) {
		check_selected();
	}
	else if (step == 2) {
		// project_save() runs on this frame
	}
	else if (step == 3) {
		step_reopen();
	}
	else if (step == 4) {
		check_reopened();
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
	file_save = string("%s/p2_baking.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
