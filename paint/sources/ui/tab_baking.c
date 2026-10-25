#include "../global.h"

// Baking tab: the settings every mesh map shares, the settings of each map and the maps to bake

static bool tab_baking_common_open    = true;
static bool tab_baking_high_poly_open = true;
static bool tab_baking_map_open[MESH_MAP_COUNT];

static i32 tab_baking_res[] = {128, 256, 512, 1024, 2048, 4096, 8192, 16384};

#define TAB_BAKING_RES_COUNT 8

static void tab_baking_progress() {
	i32   slot          = util_bake_current_slot();
	f32   progress      = util_bake_progress();
	char *name          = slot >= 0 ? util_bake_slot_name(slot) : tr("Bake Texture");
	i32   left          = util_bake_queued();
	char *label         = left > 0 ? string_tmp("%s %d%% (+%d)", name, (i32)(progress * 100), left) : string_tmp("%s %d%%", name, (i32)(progress * 100));
	i32   _col          = g_theme->BUTTON_COL;
	g_theme->BUTTON_COL = g_theme->HIGHLIGHT_COL;
	ui_slider(&progress, label, 0.0, 1.0, true, 100, false, UI_ALIGN_CENTER, true);
	g_theme->BUTTON_COL = _col;
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Click to cancel"));
		if (g_ui->input_released) {
			util_bake_cancel();
		}
	}
}

static void tab_baking_common(bake_settings_t *s) {
	// The standard sizes, plus the current one when a script set another
	i32   sizes[TAB_BAKING_RES_COUNT + 1];
	void *names[TAB_BAKING_RES_COUNT + 1];
	i32   count = 0;
	i32   index = 0;
	for (i32 i = 0; i < TAB_BAKING_RES_COUNT; ++i) {
		if (s->res > (i == 0 ? 0 : tab_baking_res[i - 1]) && s->res < tab_baking_res[i]) {
			index          = count;
			sizes[count]   = s->res;
			names[count++] = string_tmp("%d", s->res);
		}
		if (tab_baking_res[i] == s->res) {
			index = count;
		}
		sizes[count]   = tab_baking_res[i];
		names[count++] = string_tmp("%d", tab_baking_res[i]);
	}
	i32 shown = index;
	ui_combo(&index, any_array_create_from_raw_tmp(names, count), tr("Resolution"), true, UI_ALIGN_LEFT, true);
	if (index != shown) {
		s->res = sizes[index];
	}

	f32 dilation = s->dilation;
	ui_slider(&dilation, tr("Dilation"), 0, 64, true, 1, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		s->dilation = (i32)dilation;
	}

	i32 aa = s->aa >= 8 ? 3 : s->aa >= 4 ? 2 : s->aa >= 2 ? 1 : 0;
	ui_combo(&aa, any_array_create_from_raw_tmp((void *[]){tr("None"), "2x2", "4x4", "8x8"}, 4), tr("Antialiasing"), true, UI_ALIGN_LEFT, true);
	if (ui_item_changed()) {
		s->aa = 1 << aa;
	}

	ui_check(&s->auto_rebake, tr("Auto Rebake"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Bake the outdated maps again once the settings and the scene stop changing"));
	}
	ui_check(&s->quick_rebake, tr("Quick Rebake"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Auto Rebake shows a preview at a quarter of the resolution first"));
	}
}

// Objects baked from, and the rays that look for them
static void tab_baking_high_poly(bake_settings_t *s) {
	mesh_object_t_array_t *objects = g_project->_->paint_objects;
	for (i32 i = 0; i < objects->length; ++i) {
		char *name    = objects->buffer[i]->base->name;
		bool  high    = util_bake_is_high_poly(name);
		bool  by_name = s->match_by_name && high && !util_bake_listed_high_poly(name);
		ui_check(&high, by_name ? string_tmp("%s (%s)", name, tr("by name")) : name, "");
		if (ui_item_changed() && !by_name) {
			util_bake_set_high_poly(name, high);
		}
	}
	ui_check(&s->match_by_name, tr("Match by Name"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("name_low is baked only from name_high, and objects ending in _high are high poly"));
	}
	string_array_t *cages = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Fixed Distance"),
	        tr("Auto"),
	    },
	    2);
	ui_combo(&s->cage, cages, tr("Cage"), true, UI_ALIGN_LEFT, true);
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Auto starts each ray just past the high poly above the low poly, up to Max Frontal Distance"));
	}
	f32 front = s->front_distance;
	ui_slider(&front, tr("Max Frontal Distance"), 0.0, 1.0, true, 1000, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		s->front_distance = front;
	}
	f32 rear = s->rear_distance;
	ui_slider(&rear, tr("Max Rear Distance"), 0.0, 1.0, true, 1000, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		s->rear_distance = rear;
	}
	ui_check(&s->average_normals, tr("Average Normals"), "");
	ui_check(&s->ignore_backface, tr("Ignore Backface"), "");
}

static bool tab_baking_skew_open = false;
static bool tab_baking_cage_open = false;

// The gray painted into the skew and cage maps, and the buttons that select, fill and clear one
static void tab_baking_role(bake_settings_t *s, i32 role) {
	slot_layer_t *l = util_bake_role_layer(role);
	ui_row3();
	if (ui_icon_button(tr("Paint"), ICON_BRUSH, UI_ALIGN_CENTER)) {
		util_bake_paint_role(role);
	}
	g_ui->enabled = l != NULL;
	if (ui_icon_button(tr("Fill"), ICON_BAKE, UI_ALIGN_CENTER)) {
		util_bake_role_fill(role, s->paint_value);
	}
	if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
		util_bake_role_fill(role, -1.0);
	}
	g_ui->enabled = true;
	ui_slider(&s->paint_value, tr("Value"), 0.0, 1.0, true, 100, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed() && g_context->layer->bake_role != BAKE_ROLE_NONE) {
		make_material_parse_paint_material(true);
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Gray painted and filled into the skew and cage maps, whatever the material"));
	}
}

static void tab_baking_skew(bake_settings_t *s) {
	tab_baking_role(s, BAKE_ROLE_SKEW);
	string_array_t *bases = any_array_create_from_raw_tmp(
	    (void *[]){
	        tr("Mesh"),
	        tr("Per Triangle"),
	    },
	    2);
	ui_combo(&s->skew_base, bases, tr("Base Normal"), true, UI_ALIGN_LEFT, true);
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Where nothing is painted, rays follow the smooth normals of the mesh or the normal of each triangle"));
	}
	ui_check(&s->edge_protection, tr("Edge Protection"), "");
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Near hard edges rays keep the smooth normals, turning them there would open gaps"));
	}
}

static void tab_baking_cage(bake_settings_t *s) {
	tab_baking_role(s, BAKE_ROLE_CAGE);
	f32 range = s->cage_range;
	ui_slider(&range, tr("Range"), 0.0, 1.0, true, 1000, true, UI_ALIGN_RIGHT, true);
	if (ui_item_changed()) {
		s->cage_range = range < 0.0 ? 0.0 : range; // Typed values above 1 are kept
	}
	if (g_ui->is_hovered) {
		ui_tooltip(tr("Offset of the cage where the cage map is white, relative to the size of the scene. Middle gray is no offset, black pulls the cage in"));
	}
}

// Settings that change only this map, false when it has none
static bool tab_baking_map_settings(bake_settings_t *s, i32 slot) {
	if (slot == MESH_MAP_OCCLUSION || slot == MESH_MAP_BENT_NORMAL) {
		f32 samples = s->occlusion_samples;
		ui_slider(&samples, tr("Samples"), 1, 1024, true, 1, true, UI_ALIGN_RIGHT, true);
		if (ui_item_changed()) {
			s->occlusion_samples = (i32)samples;
		}
		ui_slider(&s->occlusion_distance, tr("Max Distance"), 0.0, 1.0, true, 100, true, UI_ALIGN_RIGHT, true);
		if (slot == MESH_MAP_BENT_NORMAL) {
			ui_text(tr("Shared with Ambient Occlusion"), UI_ALIGN_LEFT, 0);
		}
		return true;
	}
	if (slot == MESH_MAP_THICKNESS) {
		f32 samples = s->thickness_samples;
		ui_slider(&samples, tr("Samples"), 1, 1024, true, 1, true, UI_ALIGN_RIGHT, true);
		if (ui_item_changed()) {
			s->thickness_samples = (i32)samples;
		}
		ui_slider(&s->thickness_distance, tr("Max Distance"), 0.0, 1.0, true, 100, true, UI_ALIGN_RIGHT, true);
		return true;
	}
	if (slot == MESH_MAP_CURVATURE) {
		string_array_t *sources = any_array_create_from_raw_tmp(
		    (void *[]){
		        tr("Normals"),
		        tr("Mesh"),
		    },
		    2);
		ui_combo(&s->curvature_mode, sources, tr("Source"), true, UI_ALIGN_LEFT, true);
		if (g_ui->is_hovered) {
			ui_tooltip(tr("Mesh measures the bend of the geometry, also across uv seams"));
		}
		ui_slider(&s->curvature_strength, tr("Strength"), 0.0, 4.0, true, 100, true, UI_ALIGN_RIGHT, true);
		return true;
	}
	if (slot == MESH_MAP_ID) {
		string_array_t *sources = any_array_create_from_raw_tmp(
		    (void *[]){
		        tr("Object"),
		        tr("Vertex Color"),
		        tr("UV Island"),
		    },
		    3);
		ui_combo(&s->id_source, sources, tr("Color Source"), true, UI_ALIGN_LEFT, true);
		return true;
	}
	return false;
}

static void tab_baking_map(bake_settings_t *s, i32 slot, bool outdated) {
	gpu_texture_t *tex  = util_bake_mesh_map(slot);
	bool           on   = (s->maps >> slot) & 1;
	char          *name = outdated ? string_tmp("%s (%s)", util_bake_slot_name(slot), tr("outdated")) : util_bake_slot_name(slot);

	ui_row2();
	ui_check(&on, name, "");
	s->maps = on ? (s->maps | (1 << slot)) : (s->maps & ~(1 << slot));
	ui_panel(&tab_baking_map_open[slot], tr("Settings"), false, false, true);

	if (tab_baking_map_open[slot]) {
		tab_baking_map_settings(s, slot);
		if (tex == NULL) {
			ui_text(tr("Not baked"), UI_ALIGN_LEFT, 0);
		}
		else {
			i32 h = (i32)(g_ui->_w / UI_SCALE());
			h     = h > 128 ? 128 : h;
			ui_image(tex, 0xffffffff, h * UI_SCALE());
		}
		ui_row2();
		i32 state = util_bake_state(slot, NULL);
		if (state != BAKE_STATE_IDLE) {
			if (ui_icon_button(state == BAKE_STATE_QUEUED ? tr("Queued") : tr("Cancel"), ICON_STOP, UI_ALIGN_CENTER)) {
				util_bake_cancel_job(slot, NULL);
			}
		}
		else if (ui_icon_button(tr("Bake"), ICON_BAKE, UI_ALIGN_CENTER)) {
			util_bake_run(slot, NULL, NULL);
		}
		if (ui_icon_button(tr("Clear"), ICON_ERASE, UI_ALIGN_CENTER)) {
			util_bake_clear(slot);
		}
	}
}

void tab_baking_draw(i32 *htab) {
	if (!ui_tab(htab, tr("Baking"), false, -1, false) || g_ui->_window_h <= ui_statusbar_default_h * UI_SCALE()) {
		return;
	}
	bake_settings_t *s        = util_bake_get_settings();
	i32              outdated = util_bake_outdated_mask();
	i32              count    = 0;
	for (i32 slot = 0; slot < MESH_MAP_COUNT; ++slot) {
		count += (outdated >> slot) & 1;
	}

	ui_begin_sticky();
	if (util_bake_busy()) {
		tab_baking_progress();
	}
	else {
		ui_row2();
		if (ui_icon_button(tr("Bake Selected"), ICON_BAKE, UI_ALIGN_CENTER)) {
			util_bake_run_selected(NULL, NULL);
		}
		if (ui_icon_button(tr("Bake All"), ICON_BAKE, UI_ALIGN_CENTER)) {
			util_bake_run_all(NULL, NULL);
		}
		if (count > 0 && ui_icon_button(string_tmp("%s (%d)", tr("Rebake Outdated"), count), ICON_BAKE, UI_ALIGN_CENTER)) {
			util_bake_run_outdated();
		}
	}
	ui_end_sticky();

	if (ui_panel(&tab_baking_common_open, tr("Common Settings"), false, false, false)) {
		tab_baking_common(s);
	}
	if (ui_panel(&tab_baking_high_poly_open, tr("High Poly"), false, false, false)) {
		tab_baking_high_poly(s);
	}
	if (ui_panel(&tab_baking_skew_open, tr("Skew Map"), false, false, false)) {
		tab_baking_skew(s);
	}
	if (ui_panel(&tab_baking_cage_open, tr("Cage Map"), false, false, false)) {
		tab_baking_cage(s);
	}

	ui_separator(4, true);
	ui_text(tr("Mesh Maps"), UI_ALIGN_LEFT, 0);
	for (i32 slot = 0; slot < MESH_MAP_COUNT; ++slot) {
		tab_baking_map(s, slot, (outdated >> slot) & 1);
	}
}
