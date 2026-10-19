/* Browse and Favorites pages, plus the 96 px drink row shared with My Bar. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

/* ---- Drink row -------------------------------------------------------- */

enum { ROW_IMG, ROW_NAME, ROW_SUB, ROW_TAGS, ROW_HEART };

lv_obj_t *ui_drink_row_create(lv_obj_t *list, void *user)
{
    (void)user;
    lv_obj_t *row = ui_box(list);
    lv_obj_set_style_bg_color(row, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, ui_c(UI_C_SURFACE2), LV_STATE_PRESSED);
    lv_obj_set_style_border_side(row, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(row, 1, 0);
    lv_obj_set_style_border_color(row, ui_c(UI_C_BG), 0);

    lv_obj_t *img = lv_image_create(row);                       /* ROW_IMG */
    lv_obj_set_size(img, UI_THUMB, UI_THUMB);
    lv_obj_set_pos(img, 0, 0);

    lv_obj_t *name = ui_label(row, &adb_font_20, UI_C_TEXT, ""); /* ROW_NAME */
    lv_obj_set_pos(name, UI_THUMB + 12, 12);
    lv_obj_set_width(name, UI_HOR_RES - UI_THUMB - 12 - 36);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *sub = ui_label(row, &adb_font_14, UI_C_MUTED, ""); /* ROW_SUB */
    lv_obj_set_pos(sub, UI_THUMB + 12, 40);
    lv_obj_set_width(sub, UI_HOR_RES - UI_THUMB - 12 - 12);
    lv_label_set_long_mode(sub, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *tags = ui_box(row);                                /* ROW_TAGS */
    lv_obj_set_pos(tags, UI_THUMB + 12, 64);
    lv_obj_set_size(tags, UI_HOR_RES - UI_THUMB - 24, 22);
    lv_obj_set_flex_flow(tags, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(tags, 6, 0);
    ui_small_tag(tags, "");
    ui_small_tag(tags, "");
    lv_obj_t *miss = ui_label(tags, &adb_font_14, UI_C_ACCENT, "");
    lv_obj_set_size(miss, UI_HOR_RES - UI_THUMB - 24, 20);
    lv_label_set_long_mode(miss, LV_LABEL_LONG_MODE_DOTS);

    lv_obj_t *heart = ui_label(row, &adb_font_16, UI_C_HEART, UI_SYM_HEART); /* ROW_HEART */
    lv_obj_set_pos(heart, UI_HOR_RES - 30, 16);
    return row;
}

void ui_drink_row_bind(lv_obj_t *row, uint16_t recipe, const char *subtitle_override)
{
    const adb_recipe_t *r = &g_ui.db->recipes[recipe];
    lv_image_set_src(lv_obj_get_child(row, ROW_IMG), ui_recipe_thumb(recipe));
    lv_label_set_text(lv_obj_get_child(row, ROW_NAME), r->name);

    char sub[96];
    const char *spirit = adb_recipe_spirit_label(r);
    const char *method = r->method != ADB_NONE ? adb_tag_label(ADB_TAG_METHOD, r->method) : "";
    snprintf(sub, sizeof sub, "%s%s%s", spirit, (*spirit && *method) ? "  \xE2\x80\xA2  " : "", method);
    lv_label_set_text(lv_obj_get_child(row, ROW_SUB), sub);

    lv_obj_t *tags = lv_obj_get_child(row, ROW_TAGS);
    lv_obj_t *miss = lv_obj_get_child(tags, 2);
    int fl[2];
    int nf = subtitle_override ? 0 : adb_recipe_flavors(r, fl, 2);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *t = lv_obj_get_child(tags, i);
        if (i < nf) {
            lv_label_set_text(t, adb_tag_label(ADB_TAG_FLAVOR, fl[i]));
            lv_obj_remove_flag(t, LV_OBJ_FLAG_HIDDEN);
        } else {
            lv_obj_add_flag(t, LV_OBJ_FLAG_HIDDEN);
        }
    }
    if (subtitle_override) {
        lv_label_set_text(miss, subtitle_override);
        lv_obj_remove_flag(miss, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(miss, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_t *heart = lv_obj_get_child(row, ROW_HEART);
    if (adb_bit_get(g_ui.st->favorites, recipe)) lv_obj_remove_flag(heart, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(heart, LV_OBJ_FLAG_HIDDEN);
}

/* ---- Browse ----------------------------------------------------------- */

static lv_obj_t *s_list, *s_chips, *s_count, *s_search_lbl, *s_filter_badge;
static uint16_t *s_idx;
static uint32_t s_n;

static void browse_bind(lv_obj_t *row, uint32_t i, void *user)
{
    (void)user;
    ui_drink_row_bind(row, s_idx[i], NULL);
}

static void browse_click(uint32_t i, void *user)
{
    (void)user;
    ui_sound(UI_SOUND_TAP);
    ui_open_detail(s_idx[i]);
}

enum { CHIP_TAG, CHIP_INC, CHIP_EXC, CHIP_TEXT };

static void chip_remove_cb(lv_event_t *e)
{
    uint32_t code = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    uint32_t type = code >> 24, payload = code & 0xFFFFFF;
    adb_filter_t *f = &g_ui.browse_filter;
    if (type == CHIP_TAG) adb_filter_set_tag(f, (adb_tag_kind_t)(payload >> 8), (int)(payload & 0xFF), false);
    else if (type == CHIP_INC || type == CHIP_EXC) adb_filter_set_ing(f, (uint16_t)payload, 0);
    else adb_filter_set_text(f, "");
    ui_sound(UI_SOUND_TOGGLE);
    ui_browse_refresh();
}

static void add_chip(const char *text, uint32_t code)
{
    char buf[80];
    snprintf(buf, sizeof buf, "%s  " LV_SYMBOL_CLOSE, text);
    lv_obj_t *c = ui_chip(s_chips, buf, false);
    lv_obj_set_height(c, 36);
    lv_obj_set_ext_click_area(c, 4); /* 44 px touch target */
    ui_chip_set_checked(c, true);
    lv_obj_add_event_cb(c, chip_remove_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)code);
}

static void rebuild_chips(void)
{
    adb_filter_t *f = &g_ui.browse_filter;
    lv_obj_clean(s_chips);
    char buf[64];
    if (f->text[0]) {
        snprintf(buf, sizeof buf, "\xE2\x80\x9C%s\xE2\x80\x9D", f->text);
        add_chip(buf, CHIP_TEXT << 24);
    }
    for (int k = 0; k < ADB_TAG_COUNT; k++)
        for (int i = 0; i < adb_tag_count((adb_tag_kind_t)k); i++)
            if (adb_filter_has_tag(f, (adb_tag_kind_t)k, i))
                add_chip(adb_tag_label((adb_tag_kind_t)k, i), (CHIP_TAG << 24) | ((uint32_t)k << 8) | (uint32_t)i);
    for (int i = 0; i < f->n_include; i++) {
        snprintf(buf, sizeof buf, "+ %s", g_ui.db->ingredients[f->include[i]].name);
        add_chip(buf, (CHIP_INC << 24) | f->include[i]);
    }
    for (int i = 0; i < f->n_exclude; i++) {
        snprintf(buf, sizeof buf, "No %s", g_ui.db->ingredients[f->exclude[i]].name);
        add_chip(buf, (CHIP_EXC << 24) | f->exclude[i]);
    }
    if (lv_obj_get_child_count(s_chips)) lv_obj_remove_flag(s_chips, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_chips, LV_OBJ_FLAG_HIDDEN);
}

lv_obj_t *ui_browse_list(void) { return s_list; }

void ui_browse_refresh(void)
{
    adb_filter_t *f = &g_ui.browse_filter;
    s_n = (uint32_t)adb_filter_apply(g_ui.db, f, s_idx, g_ui.db->n_recipes);
    adb_sort_by_name(g_ui.db, s_idx, s_n);
    rebuild_chips();
    lv_label_set_text_fmt(s_count, "%u %s", (unsigned)s_n, s_n == 1 ? "drink" : "drinks");
    lv_label_set_text(s_search_lbl, f->text[0] ? f->text : "Search drinks");
    lv_obj_set_style_text_color(s_search_lbl, ui_c(f->text[0] ? UI_C_TEXT : UI_C_MUTED), 0);
    int active = adb_filter_active_count(f) - (f->text[0] ? 1 : 0);
    if (active > 0) {
        lv_label_set_text_fmt(s_filter_badge, "%d", active);
        lv_obj_remove_flag(s_filter_badge, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_filter_badge, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_update_layout(lv_obj_get_parent(s_list));
    ui_vlist_set_count(s_list, s_n, false);
}

static void search_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_TAP);
    ui_open_search(g_ui.browse_filter.text);
}

static void filter_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_TAP);
    ui_open_filter();
}

lv_obj_t *ui_search_field(lv_obj_t *parent, lv_obj_t **label_out)
{
    lv_obj_t *b = ui_button(parent, NULL, false);
    lv_obj_set_height(b, UI_TOUCH);
    lv_obj_set_style_radius(b, 22, 0);
    lv_obj_set_style_bg_color(b, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, ui_c(UI_C_DIVIDER), 0);
    lv_obj_set_style_pad_left(b, 16, 0);
    lv_obj_t *ic = ui_label(b, &adb_font_16, UI_C_MUTED, UI_SYM_SEARCH);
    lv_obj_align(ic, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *l = ui_label(b, &adb_font_16, UI_C_MUTED, "Search drinks");
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 26, 0);
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_set_width(l, LV_PCT(80));
    if (label_out) *label_out = l;
    return b;
}

void ui_browse_create(lv_obj_t *page)
{
    s_idx = malloc((g_ui.db->n_recipes ? g_ui.db->n_recipes : 1) * sizeof(uint16_t));
    lv_obj_set_flex_flow(page, LV_FLEX_FLOW_COLUMN);

    lv_obj_t *top = ui_box(page);
    lv_obj_set_size(top, LV_PCT(100), 56);
    lv_obj_set_style_pad_hor(top, UI_PAD, 0);
    lv_obj_set_flex_flow(top, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(top, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(top, UI_PAD, 0);
    lv_obj_t *search = ui_search_field(top, &s_search_lbl);
    lv_obj_set_flex_grow(search, 1);
    lv_obj_add_event_cb(search, search_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *fb = ui_button(top, UI_SYM_SLIDERS "  Filter", false);
    lv_obj_set_size(fb, 104, UI_TOUCH);
    lv_obj_add_event_cb(fb, filter_cb, LV_EVENT_CLICKED, NULL);
    s_filter_badge = ui_label(fb, &adb_font_12, UI_C_ON_ACCENT, "");
    lv_obj_set_style_bg_color(s_filter_badge, ui_c(UI_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(s_filter_badge, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(s_filter_badge, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_pad_hor(s_filter_badge, 5, 0);
    lv_obj_set_style_pad_ver(s_filter_badge, 1, 0);
    lv_obj_set_style_text_align(s_filter_badge, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_min_width(s_filter_badge, 18, 0);
    lv_obj_add_flag(s_filter_badge, LV_OBJ_FLAG_FLOATING);
    lv_obj_align(s_filter_badge, LV_ALIGN_TOP_RIGHT, 8, -4);

    s_chips = ui_box(page);
    lv_obj_set_size(s_chips, LV_PCT(100), 44);
    lv_obj_set_style_pad_hor(s_chips, UI_PAD, 0);
    lv_obj_set_flex_flow(s_chips, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_chips, 6, 0);
    lv_obj_add_flag(s_chips, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_chips, LV_DIR_HOR);
    lv_obj_set_scrollbar_mode(s_chips, LV_SCROLLBAR_MODE_OFF);

    s_count = ui_label(page, &adb_font_14, UI_C_MUTED, "");
    lv_obj_set_style_pad_left(s_count, 12, 0);
    lv_obj_set_style_pad_bottom(s_count, 6, 0);

    s_list = ui_vlist_create(page, UI_ROW_H, UI_CONTENT_H, ui_drink_row_create, browse_bind, browse_click, NULL);
    lv_obj_set_width(s_list, LV_PCT(100));
    lv_obj_set_flex_grow(s_list, 1);
}

/* ---- Favorites -------------------------------------------------------- */

static lv_obj_t *s_fav_list, *s_fav_empty, *s_fav_count;
static uint16_t *s_fav_idx;
static uint32_t s_fav_n;

static void fav_bind(lv_obj_t *row, uint32_t i, void *user)
{
    (void)user;
    ui_drink_row_bind(row, s_fav_idx[i], NULL);
}

static void fav_click(uint32_t i, void *user)
{
    (void)user;
    ui_sound(UI_SOUND_TAP);
    ui_open_detail(s_fav_idx[i]);
}

void ui_favorites_refresh(void)
{
    adb_filter_t f;
    adb_filter_clear(&f);
    f.restrict_set = g_ui.st->favorites;
    s_fav_n = (uint32_t)adb_filter_apply(g_ui.db, &f, s_fav_idx, g_ui.db->n_recipes);
    adb_sort_by_name(g_ui.db, s_fav_idx, s_fav_n);
    lv_label_set_text_fmt(s_fav_count, "%u", (unsigned)s_fav_n);
    if (s_fav_n) {
        lv_obj_add_flag(s_fav_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_fav_list, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_remove_flag(s_fav_empty, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_fav_list, LV_OBJ_FLAG_HIDDEN);
    }
    ui_vlist_set_count(s_fav_list, s_fav_n, true);
}

void ui_favorites_create(lv_obj_t *page)
{
    s_fav_idx = malloc((g_ui.db->n_recipes ? g_ui.db->n_recipes : 1) * sizeof(uint16_t));
    lv_obj_t *h = ui_header(page, "Favorites", NULL);
    s_fav_count = ui_label(h, &adb_font_14, UI_C_MUTED, "");
    lv_obj_align(s_fav_count, LV_ALIGN_RIGHT_MID, -14, 0);

    s_fav_list = ui_vlist_create(page, UI_ROW_H, UI_CONTENT_H - 52, ui_drink_row_create, fav_bind, fav_click, NULL);
    lv_obj_set_size(s_fav_list, LV_PCT(100), UI_CONTENT_H - 52);
    lv_obj_set_pos(s_fav_list, 0, 52);

    s_fav_empty = ui_box(page);
    lv_obj_set_size(s_fav_empty, LV_PCT(100), UI_CONTENT_H - 52);
    lv_obj_set_pos(s_fav_empty, 0, 52);
    lv_obj_set_flex_flow(s_fav_empty, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_fav_empty, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_fav_empty, 12, 0);
    ui_label(s_fav_empty, &adb_font_28, UI_C_DIVIDER, UI_SYM_HEART);
    lv_obj_t *t = ui_label(s_fav_empty, &adb_font_16, UI_C_MUTED,
                           "No favorites yet.\nTap the heart on a drink\nto keep it here.");
    lv_obj_set_style_text_align(t, LV_TEXT_ALIGN_CENTER, 0);
}
