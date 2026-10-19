/* Full-screen filter sheet with collapsible sections. Edits the Browse
 * filter live; the footer button shows the live result count. */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

#define KIND_INGREDIENTS ADB_TAG_COUNT

static const int k_order[] = {
    ADB_TAG_ALCOHOLIC, ADB_TAG_SPIRIT, ADB_TAG_FLAVOR, ADB_TAG_STRENGTH, ADB_TAG_SEASON,
    ADB_TAG_OCCASION, ADB_TAG_GLASS, ADB_TAG_METHOD, ADB_TAG_DIFFICULTY, KIND_INGREDIENTS,
};
#define N_SECTIONS (int)(sizeof(k_order) / sizeof(k_order[0]))

static bool s_expanded[ADB_TAG_COUNT + 1] = {
    [ADB_TAG_ALCOHOLIC] = true, [ADB_TAG_SPIRIT] = true, [ADB_TAG_FLAVOR] = true,
};
static lv_obj_t *s_ov, *s_show_btn, *s_body;
static lv_obj_t *s_sec_count[ADB_TAG_COUNT + 1], *s_sec_body[ADB_TAG_COUNT + 1], *s_sec_chev[ADB_TAG_COUNT + 1];

static void build(void);

static int section_active(int kind)
{
    const adb_filter_t *f = &g_ui.browse_filter;
    if (kind == KIND_INGREDIENTS) return f->n_include + f->n_exclude;
    return __builtin_popcount(f->mask[kind]);
}

static void update_counts(void)
{
    for (int s = 0; s < N_SECTIONS; s++) {
        int k = k_order[s], n = section_active(k);
        if (n) lv_label_set_text_fmt(s_sec_count[k], "%d selected", n);
        else lv_label_set_text(s_sec_count[k], "");
    }
    size_t n = adb_filter_apply(g_ui.db, &g_ui.browse_filter, NULL, 0);
    lv_label_set_text_fmt(lv_obj_get_child(s_show_btn, 0), "Show %u %s", (unsigned)n, n == 1 ? "drink" : "drinks");
}

/* ---- Ingredient chip (3 states) ---------------------------------------- */

static void style_ing_chip(lv_obj_t *chip, uint16_t ing)
{
    int st = adb_filter_ing_state(&g_ui.browse_filter, ing);
    const char *name = g_ui.db->ingredients[ing].name;
    lv_obj_t *l = lv_obj_get_child(chip, 0);
    if (st > 0) lv_label_set_text_fmt(l, LV_SYMBOL_OK " %s", name);
    else if (st < 0) lv_label_set_text_fmt(l, LV_SYMBOL_CLOSE " %s", name);
    else lv_label_set_text(l, name);
    uint32_t bg = st > 0 ? UI_C_OK : st < 0 ? UI_C_BAD : UI_C_SURFACE2;
    lv_obj_set_style_bg_color(chip, ui_c(bg), 0);
    lv_obj_set_style_border_color(chip, ui_c(st ? bg : UI_C_DIVIDER), 0);
    lv_obj_set_style_text_color(chip, ui_c(st ? UI_C_ON_ACCENT : UI_C_TEXT), 0);
}

static void chip_cb(lv_event_t *e)
{
    lv_obj_t *chip = lv_event_get_current_target_obj(e);
    uint32_t code = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    int kind = (int)(code >> 16), idx = (int)(code & 0xFFFF);
    adb_filter_t *f = &g_ui.browse_filter;
    if (kind == KIND_INGREDIENTS) {
        int st = adb_filter_ing_state(f, (uint16_t)idx);
        st = st == 0 ? 1 : st > 0 ? -1 : 0; /* none -> include -> exclude -> none */
        adb_filter_set_ing(f, (uint16_t)idx, st);
        style_ing_chip(chip, (uint16_t)idx);
    } else {
        adb_filter_toggle_tag(f, (adb_tag_kind_t)kind, idx);
        ui_chip_set_checked(chip, adb_filter_has_tag(f, (adb_tag_kind_t)kind, idx));
    }
    ui_sound(UI_SOUND_TOGGLE);
    update_counts();
}

/* ---- Sections ---------------------------------------------------------- */

static bool tag_used(int kind, int idx)
{
    const adb_db_t *db = g_ui.db;
    for (int i = 0; i < db->n_recipes; i++) {
        const adb_recipe_t *r = &db->recipes[i];
        uint32_t bit = 1u << idx;
        switch (kind) {
        case ADB_TAG_SPIRIT: if (r->spirit_mask & bit) return true; break;
        case ADB_TAG_FLAVOR: if (r->flavor_mask & bit) return true; break;
        case ADB_TAG_SEASON: if (r->season_mask & bit) return true; break;
        case ADB_TAG_OCCASION: if (r->occasion_mask & bit) return true; break;
        case ADB_TAG_STRENGTH: if (r->strength == idx) return true; break;
        case ADB_TAG_GLASS: if (r->glass == idx) return true; break;
        case ADB_TAG_METHOD: if (r->method == idx) return true; break;
        case ADB_TAG_DIFFICULTY: if (r->difficulty == idx) return true; break;
        case ADB_TAG_ALCOHOLIC: if (r->alcoholic == idx) return true; break;
        default: break;
        }
    }
    return false;
}

static lv_obj_t *chip_box(lv_obj_t *parent)
{
    lv_obj_t *b = ui_box(parent);
    lv_obj_set_size(b, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(b, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(b, 6, 0);
    lv_obj_set_style_pad_row(b, 6, 0);
    return b;
}

static void fill_section(int kind)
{
    lv_obj_t *body = s_sec_body[kind];
    if (lv_obj_get_child_count(body)) return; /* already built */
    adb_filter_t *f = &g_ui.browse_filter;
    if (kind != KIND_INGREDIENTS) {
        lv_obj_t *box = chip_box(body);
        for (int i = 0; i < adb_tag_count((adb_tag_kind_t)kind); i++) {
            if (!tag_used(kind, i) && !adb_filter_has_tag(f, (adb_tag_kind_t)kind, i)) continue;
            lv_obj_t *c = ui_chip(box, adb_tag_label((adb_tag_kind_t)kind, i), false);
            ui_chip_set_checked(c, adb_filter_has_tag(f, (adb_tag_kind_t)kind, i));
            lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(((uint32_t)kind << 16) | (uint32_t)i));
        }
        return;
    }
    lv_obj_t *hint = ui_label(body, &adb_font_12, UI_C_MUTED, "Tap once to require, twice to exclude.");
    lv_obj_set_style_pad_bottom(hint, 4, 0);
    const adb_db_t *db = g_ui.db;
    int last_cat = -1;
    lv_obj_t *box = NULL;
    for (int j = 0; j < db->n_ingredients; j++) {
        uint16_t ing = db->ing_by_category[j];
        const adb_ingredient_t *in = &db->ingredients[ing];
        if (!in->n_recipes) continue;
        if (in->category != last_cat) {
            last_cat = in->category;
            lv_obj_t *h = ui_label(body, &adb_font_12, UI_C_ACCENT, adb_tag_label(ADB_TAG_ING_CATEGORY, in->category));
            lv_obj_set_style_pad_top(h, 8, 0);
            lv_obj_set_style_pad_bottom(h, 2, 0);
            box = chip_box(body);
        }
        lv_obj_t *c = ui_chip(box, in->name, false);
        style_ing_chip(c, ing);
        lv_obj_add_event_cb(c, chip_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)(((uint32_t)KIND_INGREDIENTS << 16) | ing));
    }
}

static void apply_expanded(int kind)
{
    if (s_expanded[kind]) {
        fill_section(kind);
        lv_obj_remove_flag(s_sec_body[kind], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_sec_chev[kind], LV_SYMBOL_UP);
    } else {
        lv_obj_add_flag(s_sec_body[kind], LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_sec_chev[kind], LV_SYMBOL_DOWN);
    }
}

static void section_cb(lv_event_t *e)
{
    int kind = (int)(intptr_t)lv_event_get_user_data(e);
    s_expanded[kind] = !s_expanded[kind];
    ui_sound(UI_SOUND_TAP);
    apply_expanded(kind);
}

void ui_filter_set_expanded(int kind, bool expanded)
{
    if (kind < 0 || kind > ADB_TAG_COUNT) return;
    s_expanded[kind] = expanded;
    if (s_ov) apply_expanded(kind);
}

static void close_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_BACK);
    ui_overlay_pop();
}

static void clear_cb(lv_event_t *e)
{
    (void)e;
    adb_filter_t *f = &g_ui.browse_filter;
    char text[ADB_FILTER_TEXT_MAX];
    memcpy(text, f->text, sizeof text);
    adb_filter_clear(f);
    adb_filter_set_text(f, text); /* the search text is cleared from Browse */
    ui_sound(UI_SOUND_TOGGLE);
    build();
}

static void deleted_cb(lv_event_t *e)
{
    (void)e;
    s_ov = NULL;
}

static void build(void)
{
    lv_obj_clean(s_ov);
    lv_obj_t *h = ui_header(s_ov, "Filters", close_cb);
    lv_obj_set_pos(h, 0, 0);
    lv_obj_t *clear = ui_button(h, "Clear all", false);
    lv_obj_set_height(clear, 36);
    lv_obj_set_ext_click_area(clear, 4);
    lv_obj_set_style_text_color(clear, ui_c(UI_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(clear, LV_OPA_TRANSP, 0);
    lv_obj_align(clear, LV_ALIGN_RIGHT_MID, -4, 0);
    lv_obj_add_event_cb(clear, clear_cb, LV_EVENT_CLICKED, NULL);

    s_body = lv_obj_create(s_ov);
    lv_obj_remove_style_all(s_body);
    lv_obj_set_size(s_body, UI_HOR_RES, UI_OVERLAY_H - 52 - 68);
    lv_obj_set_pos(s_body, 0, 52);
    lv_obj_set_style_bg_color(s_body, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_body, 12, 0);
    lv_obj_set_style_pad_bottom(s_body, 12, 0);
    lv_obj_set_flex_flow(s_body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_body, LV_DIR_VER);
    lv_obj_set_style_width(s_body, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(s_body, ui_c(UI_C_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(s_body, LV_OPA_COVER, LV_PART_SCROLLBAR);

    for (int s = 0; s < N_SECTIONS; s++) {
        int k = k_order[s];
        lv_obj_t *hdr = lv_button_create(s_body);
        lv_obj_remove_style_all(hdr);
        lv_obj_set_size(hdr, LV_PCT(100), 48);
        lv_obj_set_style_border_side(hdr, LV_BORDER_SIDE_BOTTOM, 0);
        lv_obj_set_style_border_width(hdr, 1, 0);
        lv_obj_set_style_border_color(hdr, ui_c(UI_C_DIVIDER), 0);
        lv_obj_set_style_bg_color(hdr, ui_c(UI_C_SURFACE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(hdr, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_add_event_cb(hdr, section_cb, LV_EVENT_CLICKED, (void *)(intptr_t)k);
        lv_obj_t *t = ui_label(hdr, &adb_font_16, UI_C_TEXT, k == KIND_INGREDIENTS ? "Ingredients" : adb_tag_kind_label((adb_tag_kind_t)k));
        lv_obj_align(t, LV_ALIGN_LEFT_MID, 0, 0);
        s_sec_chev[k] = ui_label(hdr, &adb_font_14, UI_C_MUTED, "");
        lv_obj_align(s_sec_chev[k], LV_ALIGN_RIGHT_MID, -4, 0);
        s_sec_count[k] = ui_label(hdr, &adb_font_14, UI_C_ACCENT, "");
        lv_obj_align(s_sec_count[k], LV_ALIGN_RIGHT_MID, -28, 0);

        s_sec_body[k] = ui_box(s_body);
        lv_obj_set_size(s_sec_body[k], LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_flex_flow(s_sec_body[k], LV_FLEX_FLOW_COLUMN);
        lv_obj_set_style_pad_ver(s_sec_body[k], 10, 0);
        apply_expanded(k);
    }

    lv_obj_t *foot = ui_box(s_ov);
    lv_obj_set_size(foot, UI_HOR_RES, 68);
    lv_obj_set_pos(foot, 0, UI_OVERLAY_H - 68);
    lv_obj_set_style_bg_color(foot, ui_c(UI_C_STATUS), 0);
    lv_obj_set_style_bg_opa(foot, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(foot, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(foot, 1, 0);
    lv_obj_set_style_border_color(foot, ui_c(UI_C_DIVIDER), 0);
    s_show_btn = ui_button(foot, "Show", true);
    lv_obj_set_size(s_show_btn, UI_HOR_RES - 24, 48);
    lv_obj_center(s_show_btn);
    lv_obj_add_event_cb(s_show_btn, close_cb, LV_EVENT_CLICKED, NULL);
    update_counts();
}

void ui_filter_create(lv_obj_t *ov)
{
    s_ov = ov;
    lv_obj_add_event_cb(ov, deleted_cb, LV_EVENT_DELETE, NULL);
    build();
}
