#include "global.h"

// Builds fixtures/p2_roles_project.arm, a project of format version 23 saved by the P2 builds made
// before the Paint effect and the face selection added their layer fields: 45 keys per layer, with
// bake_role at key 45, where effect_paint is now. It has a skew map and a cage map.
// p2_role_layers.c opens it to check that the roles come back and no layer turns into a Paint effect.
// It was made with the build of commit da6d9b9, a newer build would save the fields of the P1 too and
// the fixture would no longer test that.
// Run with: ARMORPAINT_APP=<build of da6d9b9> base/tests/paint_headless/run.sh make_fixture_p2_roles.c

char *root     = "../../../base/tests/paint_headless";
char *file_arm = NULL;

int step   = 0;
int wait   = 0;
int failed = 0;

void fail(char *what) {
	failed = 1;
	printf("[fixture] FAIL %s", what);
}

int role_count(int role) {
	int n = 0;
	for (int i = 0; i < script_layer_count(); ++i) {
		if (script_layer_at(i)->bake_role == role) {
			n += 1;
		}
	}
	return n;
}

void next_frame() {
	if (wait > 0) {
		wait -= 1;
		script_notify_on_next_frame(next_frame);
		return;
	}
	if (step == 0) {
		if (script_get_config()->tools != TOOLS_PAINT) {
			fail("not in paint mode, remove data/config.json");
		}
		script_project_new();
		wait = 2;
	}
	else if (step == 1) {
		script_bake_paint_role(BAKE_ROLE_SKEW);
		script_bake_role_fill(BAKE_ROLE_SKEW, 1.0);
		script_bake_paint_role(BAKE_ROLE_CAGE);
		script_bake_role_fill(BAKE_ROLE_CAGE, 0.75);
		wait = 2;
	}
	else if (step == 2) {
		if (script_layer_count() != 3 || role_count(BAKE_ROLE_SKEW) != 1 || role_count(BAKE_ROLE_CAGE) != 1) {
			fail(string("layers %d, skew %d, cage %d", script_layer_count(), role_count(BAKE_ROLE_SKEW), role_count(BAKE_ROLE_CAGE)));
		}
		if (iron_file_exists(file_arm)) {
			iron_delete_file(file_arm);
		}
		project_filepath_set(file_arm);
		project_save(false);
	}
	else if (step == 3) {
		// project_save() writes on the next frame
	}
	else {
		if (!iron_file_exists(file_arm)) {
			fail("fixture not saved");
		}
		printf(failed ? "FIXTURE FAIL" : "FIXTURE DONE %s", file_arm);
		script_quit();
		return;
	}
	step += 1;
	script_notify_on_next_frame(next_frame);
}

void main() {
	file_arm = string("%s/fixtures/p2_roles_project.arm", root);
	script_notify_on_next_frame(next_frame);
}
