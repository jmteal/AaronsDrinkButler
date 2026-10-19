/* My Bar: ingredient checklist grouped by category, and "What can I make?"
 * results bucketed into Can make / Missing 1 / Missing 2. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

#define ING_ROW_H 48

static lv_obj_t *s_mode_seg, *s_ing_list, *s_res_box, *s_bucket_seg, *s_res_list, *s_res_empty;
static bool s_results;
static int s_bucket;

/* checklist rows: category headers interleaved with ingredients */
typedef struct { uint16_t ing; bool header; uint8_t cat; } ing_row_t;
static ing_row_t *s_rows;
static uint32_t s_n_rows;

static adb_pantry_result_t s_res;
static bool s_res_valid;

/* ---- Ingredient checklist --------------------------------------------- */

static void build_rows(void)
{
    const adb_db_t *db = g_ui.db;
    free(s_rows);
    s_rows = malloc((size_t)(db->n_ingredients * 2 + 1) * sizeof(ing_row_t));
    s_n_rows = 0;
    int last = -1;
    for (int j = 0; j < db->n_ingredients; j++) {
        uint16_t ing = db->ing_by_category[j];
        const adb_ingredient_t *in = &db->ingredients[ing];
        /* garnish and water never block a recipe, so don't ask about them */
        if (!in->n_recipes || in->category == ADB_CAT_GARNISH || !strcmp(in->id, "water")) continue;
        if (in->category != last) {
            last = in->category;
            s_rows[s_n_rows++] = (ing_row_t){ .header = true, .cat = in->category };
        }
        s_rows[s_n_rows++] = (ing_row_t){ .ing = ing };
    }
}

static lv_obj_t *ing_row_create(lv_obj_t *list, void *user)
{
    (void)user;
    lv_obj_t *row = ui_box(list);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, ui_c(UI_C_BG), 0);
    lv_obj_t *name = ui_label(row, &adb_font_16, UI_C_TEXT, "");       /* 0 */
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);
    lv_obj_set_size(name, UI_HOR_RES - 16 - 130, 20);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_t *sub = ui_label(row, &adb_font_12, UI_C_MUTED, "");       /* 1 */
    lv_obj_align(sub, LV_ALIGN_RIGHT_MID, -60, 0);
    lv_obj_t *box = ui_box(row);                                        /* 2: checkbox */
    lv_obj_set_size(box, 28, 28);
    lv_obj_align(box, LV_ALIGN_RIGHT_MID, -16, 0);
    lv_obj_set_style_radius(box, 7, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_t *tick = ui_label(box, &adb_font_16, UI_C_ON_ACCENT, LV_SYMBOL_OK);
    lv_obj_center(tick);
    return row;
}

static void ing_row_bind(lv_obj_t *row, uint32_t i, void *user)
{
    (void)user;
    const ing_row_t *r = &s_rows[i];
    lv_obj_t *name = lv_obj_get_child(row, 0), *sub = lv_obj_get_child(row, 1), *box = lv_obj_get_child(row, 2);
    if (r->header) {
        lv_obj_set_style_bg_color(row, ui_c(UI_C_BG), 0);
        lv_obj_set_style_bg_color(row, ui_c(UI_C_BG), LV_STATE_PRESSED);
        lv_obj_set_style_text_font(name, &adb_font_14, 0);
        lv_obj_set_style_text_color(name, ui_c(UI_C_ACCENT), 0);
        lv_obj_align(name, LV_ALIGN_BOTTOM_LEFT, 16, -6);
        lv_label_set_text(name, adb_tag_label(ADB_TAG_ING_CATEGORY, r->cat));
        lv_label_set_text(sub, "");
        lv_obj_add_flag(box, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    const adb_ingredient_t *in = &g_ui.db->ingredients[r->ing];
    bool have = adb_bit_get(g_ui.st->pantry, r->ing);
    lv_obj_set_style_bg_color(row, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_bg_color(row, ui_c(UI_C_SURFACE2), LV_STATE_PRESSED);
    lv_obj_set_style_text_font(name, &adb_font_16, 0);
    lv_obj_set_style_text_color(name, ui_c(UI_C_TEXT), 0);
    lv_obj_align(name, LV_ALIGN_LEFT_MID, 16, 0);
    lv_label_set_text(name, in->name);
    lv_label_set_text_fmt(sub, "%u %s", in->n_recipes, in->n_recipes == 1 ? "drink" : "drinks");
    lv_obj_remove_flag(box, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_color(box, ui_c(have ? UI_C_ACCENT : UI_C_SURFACE), 0);
    lv_obj_set_style_border_color(box, ui_c(have ? UI_C_ACCENT : UI_C_MUTED), 0);
    lv_obj_t *tick = lv_obj_get_child(box, 0);
    if (have) lv_obj_remove_flag(tick, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(tick, LV_OBJ_FLAG_HIDDEN);
}

static void update_mode_labels(void);

static void ing_click(uint32_t i, void *user)
{
    (void)user;
    const ing_row_t *r = &s_rows[i];
    if (r->header) return;
    bool have = !adb_bit_get(g_ui.st->pantry, r->ing);
    adb_bit_set(g_ui.st->pantry, r->ing, have);
    s_res_valid = false;
    ui_sound(UI_SOUND_TOGGLE);
    ui_save_pantry();
    ui_vlist_rebind(s_ing_list);
    update_mode_labels();
}

/* ---- Results ----------------------------------------------------------- */

static void ensure_results(void)
{
    if (s_res_valid) return;
    adb_pantry_result_free(&s_res);
    adb_pantry_compute(g_ui.db, g_ui.st->pantry, &s_res);
    s_res_valid = true;
}

static lv_obj_t *res_row_create(lv_obj_t *list, void *user) { return ui_drink_row_create(list, user); }

static void res_row_bind(lv_obj_t *row, uint32_t i, void *user)
{
    (void)user;
    uint16_t recipe = s_res.idx[s_bucket][i];
    if (s_bucket == 0) {
        ui_drink_row_bind(row, recipe, "You have everything");
        return;
    }
    uint16_t miss[2];
    int n = adb_pantry_missing(g_ui.db, g_ui.st->pantry, recipe, miss, 2);
    char buf[128];
    if (n >= 2)
        snprintf(buf, sizeof buf, "Need: %s, %s", g_ui.db->ingredients[miss[0]].name, g_ui.db->ingredients[miss[1]].name);
    else
        snprintf(buf, sizeof buf, "Need: %s", n ? g_ui.db->ingredients[miss[0]].name : "?");
    ui_drink_row_bind(row, recipe, buf);
}

static void res_click(uint32_t i, void *user)
{
    (void)user;
    ui_sound(UI_SOUND_TAP);
    ui_open_detail(s_res.idx[s_bucket][i]);
}

static int pantry_count(void)
{
    int n = 0;
    for (int i = 0; i < g_ui.db->n_ingredients; i++) n += adb_bit_get(g_ui.st->pantry, (unsigned)i);
    return n;
}

static void update_mode_labels(void)
{
    char buf[40];
    snprintf(buf, sizeof buf, "My bottles (%d)", pantry_count());
    lv_label_set_text(lv_obj_get_child(lv_obj_get_child(s_mode_seg, 0), 0), buf);
}

static void refresh_results(void)
{
    ensure_results();
    static const char *names[3] = { "Can make", "Missing 1", "Missing 2" };
    for (int b = 0; b < 3; b++)
        lv_label_set_text_fmt(lv_obj_get_child(lv_obj_get_child(s_bucket_seg, b), 0), "%s (%u)", names[b], s_res.n[b]);
    ui_segmented_select(s_bucket_seg, s_bucket);
    uint32_t n = s_res.n[s_bucket];
    if (n) {
        lv_obj_add_flag(s_res_empty, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_res_empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_res_empty, pantry_count() == 0
            ? "Tick the bottles and mixers you have\nunder \"My bottles\" to see what\nyou can make."
            : s_bucket == 0 ? "Nothing complete yet.\nCheck Missing 1 for close calls."
                            : "No drinks in this group.");
    }
    ui_vlist_set_count(s_res_list, n, false);
}

void ui_mybar_refresh(void)
{
    update_mode_labels();
    ui_segmented_select(s_mode_seg, s_results ? 1 : 0);
    if (s_results) {
        lv_obj_add_flag(s_ing_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_res_box, LV_OBJ_FLAG_HIDDEN);
        refresh_results();
    } else {
        lv_obj_remove_flag(s_ing_list, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_res_box, LV_OBJ_FLAG_HIDDEN);
        ui_vlist_set_count(s_ing_list, s_n_rows, true);
    }
}

void ui_mybar_show(bool results, int bucket)
{
    s_results = results;
    if (bucket >= 0 && bucket <= 2) s_bucket = bucket;
    s_res_valid = false;
    if (g_ui.tab != UI_TAB_MYBAR) ui_show_tab(UI_TAB_MYBAR);
    else ui_mybar_refresh();
}

static void mode_cb(lv_event_t *e)
{
    s_results = ui_segmented_get(lv_event_get_current_target_obj(e)) == 1;
    ui_mybar_refresh();
}

static void bucket_cb(lv_event_t *e)
{
    s_bucket = ui_segmented_get(lv_event_get_current_target_obj(e));
    refresh_results();
}

void ui_mybar_create(lv_obj_t *page)
{
    build_rows();
    static const char *const modes[] = { "My bottles", "What can I make?" };
    s_mode_seg = ui_segmented(page, modes, 2, 0, mode_cb, NULL);
    lv_obj_set_size(s_mode_seg, UI_HOR_RES - 16, UI_TOUCH);
    lv_obj_set_pos(s_mode_seg, 8, 6);
    int top = 6 + UI_TOUCH + 6;

    s_ing_list = ui_vlist_create(page, ING_ROW_H, UI_CONTENT_H - top, ing_row_create, ing_row_bind, ing_click, NULL);
    lv_obj_set_size(s_ing_list, UI_HOR_RES, UI_CONTENT_H - top);
    lv_obj_set_pos(s_ing_list, 0, top);

    s_res_box = ui_box(page);
    lv_obj_set_size(s_res_box, UI_HOR_RES, UI_CONTENT_H - top);
    lv_obj_set_pos(s_res_box, 0, top);
    static const char *const buckets[] = { "Can make", "Missing 1", "Missing 2" };
    s_bucket_seg = ui_segmented(s_res_box, buckets, 3, 0, bucket_cb, NULL);
    lv_obj_set_size(s_bucket_seg, UI_HOR_RES - 16, UI_TOUCH);
    lv_obj_set_pos(s_bucket_seg, 8, 0);
    lv_obj_set_style_bg_color(s_bucket_seg, ui_c(UI_C_SURFACE), 0);
    for (int i = 0; i < 3; i++) lv_obj_set_style_text_font(lv_obj_get_child(s_bucket_seg, i), &adb_font_12, 0);
    int rt = UI_TOUCH + 6;
    s_res_list = ui_vlist_create(s_res_box, UI_ROW_H, UI_CONTENT_H - top - rt, res_row_create, res_row_bind, res_click, NULL);
    lv_obj_set_size(s_res_list, UI_HOR_RES, UI_CONTENT_H - top - rt);
    lv_obj_set_pos(s_res_list, 0, rt);
    s_res_empty = ui_label(s_res_box, &adb_font_16, UI_C_MUTED, "");
    lv_obj_set_style_text_align(s_res_empty, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_res_empty, LV_ALIGN_CENTER, 0, 10);
    lv_obj_add_flag(s_res_box, LV_OBJ_FLAG_HIDDEN);
}
