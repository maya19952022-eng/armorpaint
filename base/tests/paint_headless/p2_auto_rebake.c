#include "global.h"

// BAKE-05: outdated mesh maps, rebake outdated, auto rebake and quick rebake.
// Run with: base/tests/paint_headless/run.sh p2_auto_rebake.c

char *dir_out   = "../smoke";
char *file_save = NULL;

int step     = 0;
int checks   = 0;
int failures = 0;
int waited   = 0;

int    history0  = 0;
int    saw_quick = 0;
double time0     = 0.0;

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

int width(int slot) {
	gpu_texture_t *t = script_mesh_map_get(slot);
	return t != NULL ? t->width : 0;
}

object_t *cube() {
	return script_get_context()->paint_object->base;
}

void step_build() {
	script_project_new();
	bake_settings_t *s = script_bake_get_settings();
	check(!s->auto_rebake && s->quick_rebake, "auto rebake is off, quick rebake on");
	s->res               = 32;
	s->dilation          = 2;
	s->occlusion_samples = 8;
	s->maps              = (1 << MESH_MAP_OCCLUSION) | (1 << MESH_MAP_CURVATURE);
	script_bake_selected();
}

void check_settings_change() {
	check(script_mesh_map_get(MESH_MAP_OCCLUSION) != NULL && script_mesh_map_get(MESH_MAP_CURVATURE) != NULL, "two maps baked");
	check(!script_bake_outdated(MESH_MAP_OCCLUSION) && !script_bake_outdated(MESH_MAP_CURVATURE), "fresh maps are up to date");
	check(!script_bake_outdated(MESH_MAP_THICKNESS), "a slot without a map is not outdated");
	script_bake_get_settings()->occlusion_samples = 16;
	check(script_bake_outdated(MESH_MAP_OCCLUSION), "occlusion is outdated after its samples change");
	check(!script_bake_outdated(MESH_MAP_CURVATURE), "curvature does not use the occlusion samples");
	history0 = script_history_undos();
	check(script_bake_rebake_outdated() == 1, "rebake outdated queues the one outdated map");
}

void check_rebaked() {
	check(!script_bake_outdated(MESH_MAP_OCCLUSION), "rebaked occlusion is up to date");
	check(script_history_undos() == history0 + 1, "a rebake is a history step");
	script_undo();
	check(script_bake_outdated(MESH_MAP_OCCLUSION), "undo brings back the map of the old samples, outdated");
	script_redo();
	check(!script_bake_outdated(MESH_MAP_OCCLUSION), "redo brings back the up to date map");

	// Moving the object changes what every map is baked from
	object_t *o         = cube();
	o->transform->loc.x = o->transform->loc.x + 0.5;
	transform_build_matrix(o->transform);
	check(script_bake_outdated(MESH_MAP_OCCLUSION) && script_bake_outdated(MESH_MAP_CURVATURE), "moving the object makes both maps outdated");

	bake_settings_t *s = script_bake_get_settings();
	s->auto_rebake     = true;
	s->quick_rebake    = false;
	history0           = script_history_undos();
	time0              = sys_time();
}

void check_auto() {
	check(!script_bake_outdated(MESH_MAP_OCCLUSION) && !script_bake_outdated(MESH_MAP_CURVATURE), "auto rebake brings both maps up to date");
	check(script_history_undos() == history0, "auto rebake leaves no history step");
	check(sys_time() - time0 >= 0.5, string("auto rebake waits for the changes to stop (%.2f s)", sys_time() - time0));

	script_mesh_map_clear(MESH_MAP_OCCLUSION);
	check(!script_bake_outdated(MESH_MAP_OCCLUSION), "a cleared slot is not outdated");
	bake_settings_t *s = script_bake_get_settings();
	s->quick_rebake    = true;
	s->res             = 512;
}

void check_quick() {
	check(saw_quick, "quick rebake shows a quarter resolution preview first");
	check(width(MESH_MAP_CURVATURE) == 512 && !script_bake_outdated(MESH_MAP_CURVATURE), string("then the full bake (%d)", width(MESH_MAP_CURVATURE)));
	check(script_mesh_map_get(MESH_MAP_OCCLUSION) == NULL, "auto rebake leaves empty slots empty");
	bake_settings_t *s    = script_bake_get_settings();
	s->quick_rebake       = false;
	s->curvature_strength = 2.0;
}

void cancel_auto() {
	check(script_bake_state(MESH_MAP_CURVATURE) != BAKE_STATE_IDLE, "auto rebake started after a change");
	script_bake_cancel_slot(MESH_MAP_CURVATURE);
	time0 = sys_time();
}

void check_cancelled() {
	check(script_bake_state(MESH_MAP_CURVATURE) == BAKE_STATE_IDLE && script_bake_outdated(MESH_MAP_CURVATURE), "a cancelled map is not started again");
	script_bake_get_settings()->curvature_strength = 3.0;
}

void check_resumed() {
	check(!script_bake_outdated(MESH_MAP_CURVATURE), "a new change starts auto rebake again");
	if (iron_file_exists(file_save)) {
		iron_delete_file(file_save);
	}
	project_filepath_set(file_save);
	project_save(false); // Writes on the next frame
}

void step_reopen() {
	script_project_new();
	check(!script_bake_get_settings()->auto_rebake, "a new project has auto rebake off");
	script_project_open(file_save);
	bake_settings_t *s = script_bake_get_settings();
	check(s->auto_rebake && !s->quick_rebake, "auto and quick rebake survive save and load");
	check(script_mesh_map_get(MESH_MAP_CURVATURE) != NULL && !script_bake_outdated(MESH_MAP_CURVATURE), "a reopened map is still up to date");
	s->curvature_strength = 1.0;
	check(script_bake_outdated(MESH_MAP_CURVATURE), "and outdated once a setting changes");
	s->auto_rebake = false;
}

// Steps that wait for the bakes, or for auto rebake to catch up
int waiting() {
	bool busy = script_bake_busy();
	if (step == 1 || step == 2) {
		return busy;
	}
	if (step == 3) {
		return busy || script_bake_outdated(MESH_MAP_OCCLUSION) || script_bake_outdated(MESH_MAP_CURVATURE);
	}
	if (step == 4) {
		if (width(MESH_MAP_CURVATURE) == 128) {
			saw_quick = 1;
		}
		return busy || script_bake_outdated(MESH_MAP_CURVATURE);
	}
	if (step == 5) {
		return script_bake_state(MESH_MAP_CURVATURE) == BAKE_STATE_IDLE;
	}
	if (step == 6) {
		return sys_time() - time0 < 1.0;
	}
	if (step == 7) {
		return busy || script_bake_outdated(MESH_MAP_CURVATURE);
	}
	return 0;
}

void next_frame() {
	if (waiting()) {
		waited += 1;
		if (waited > 5000) {
			check(0, string("step %d finished in time", step));
			script_bake_cancel();
			waited = 0;
			step += 1;
		}
		script_notify_on_next_frame(next_frame);
		return;
	}
	waited = 0;

	if (step == 0) {
		step_build();
	}
	else if (step == 1) {
		check_settings_change();
	}
	else if (step == 2) {
		check_rebaked();
	}
	else if (step == 3) {
		check_auto();
	}
	else if (step == 4) {
		check_quick();
	}
	else if (step == 5) {
		cancel_auto();
	}
	else if (step == 6) {
		check_cancelled();
	}
	else if (step == 7) {
		check_resumed();
	}
	else if (step == 8) {
		// project_save() runs on this frame
	}
	else if (step == 9) {
		step_reopen();
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
	file_save = string("%s/p2_auto_rebake.arm", dir_out);
	iron_create_directory(dir_out);
	script_notify_on_next_frame(next_frame);
}
