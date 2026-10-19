/* Search overlay: text field, live match count and on-screen keyboard.
 * "OK" applies the text to the Browse filter and shows Browse. */
#include <stdio.h>
#include "ui_internal.h"

static lv_obj_t *s_ta, *s_count;

static void update_count(void)
{
    adb_filter_t f = g_ui.browse_filter;
    adb_filter_set_text(&f, lv_textarea_get_text(s_ta));
    size_t n = adb_filter_apply(g_ui.db, &f, NULL, 0);
    lv_label_set_text_fmt(s_count, "%u matching %s", (unsigned)n, n == 1 ? "drink" : "drinks");
}

static void changed_cb(lv_event_t *e)
{
    (void)e;
    update_count();
}

static void done(bool apply)
{
    if (apply) adb_filter_set_text(&g_ui.browse_filter, lv_textarea_get_text(s_ta));
    ui_close_overlays();
    if (apply) ui_show_tab(UI_TAB_BROWSE);
}

static void kb_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) { ui_sound(UI_SOUND_TAP); done(true); }
    else if (code == LV_EVENT_CANCEL) { ui_sound(UI_SOUND_BACK); done(false); }
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_BACK);
    done(false);
}

void ui_search_create(lv_obj_t *ov, const char *initial)
{
    lv_obj_t *h = ui_header(ov, "Search", back_cb);
    lv_obj_set_pos(h, 0, 0);

    s_ta = lv_textarea_create(ov);
    lv_textarea_set_one_line(s_ta, true);
    lv_textarea_set_max_length(s_ta, ADB_FILTER_TEXT_MAX - 1);
    lv_textarea_set_placeholder_text(s_ta, "Drink or ingredient");
    lv_textarea_set_text(s_ta, initial ? initial : "");
    lv_obj_set_size(s_ta, UI_HOR_RES - 24, 48);
    lv_obj_set_pos(s_ta, 12, 64);
    lv_obj_set_style_bg_color(s_ta, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_border_color(s_ta, ui_c(UI_C_ACCENT), 0);
    lv_obj_set_style_border_width(s_ta, 2, 0);
    lv_obj_set_style_radius(s_ta, 12, 0);
    lv_obj_set_style_text_font(s_ta, &adb_font_20, 0);
    lv_obj_set_style_text_color(s_ta, ui_c(UI_C_TEXT), 0);
    lv_obj_set_scrollbar_mode(s_ta, LV_SCROLLBAR_MODE_OFF);
    lv_obj_add_state(s_ta, LV_STATE_FOCUSED);
    lv_obj_add_event_cb(s_ta, changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_count = ui_label(ov, &adb_font_14, UI_C_MUTED, "");
    lv_obj_set_pos(s_count, 16, 122);

    lv_obj_t *kb = lv_keyboard_create(ov);
    lv_obj_set_size(kb, UI_HOR_RES, 220);
    lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_textarea(kb, s_ta);
    lv_obj_set_style_bg_color(kb, ui_c(UI_C_STATUS), 0);
    lv_obj_set_style_pad_all(kb, 3, 0);
    lv_obj_set_style_pad_gap(kb, 3, 0);
    lv_obj_set_style_bg_color(kb, ui_c(UI_C_SURFACE2), LV_PART_ITEMS);
    lv_obj_set_style_text_color(kb, ui_c(UI_C_TEXT), LV_PART_ITEMS);
    lv_obj_set_style_text_font(kb, &adb_font_16, LV_PART_ITEMS);
    lv_obj_set_style_radius(kb, 6, LV_PART_ITEMS);
    lv_obj_set_style_shadow_width(kb, 0, LV_PART_ITEMS);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_READY, NULL);
    lv_obj_add_event_cb(kb, kb_cb, LV_EVENT_CANCEL, NULL);
    update_count();
}
