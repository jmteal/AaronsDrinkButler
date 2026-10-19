/* Startup splash. The default owner (Aaron) gets one of the four logos,
 * a different one each time; anyone else gets a text splash with their
 * name. Tapping dismisses it early. */
#include <stdio.h>
#include "ui_internal.h"

LV_IMAGE_DECLARE(adb_logo_1);
LV_IMAGE_DECLARE(adb_logo_2);
LV_IMAGE_DECLARE(adb_logo_3);
LV_IMAGE_DECLARE(adb_logo_4);

static const lv_image_dsc_t *const k_logos[UI_LOGO_COUNT] = {
    &adb_logo_1, &adb_logo_2, &adb_logo_3, &adb_logo_4,
};

static lv_obj_t *s_splash;
static lv_timer_t *s_timer;

static void splash_deleted_cb(lv_event_t *e)
{
    (void)e;
    s_splash = NULL;
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
}

void ui_hide_splash(void)
{
    if (!s_splash) return;
    if (s_timer) {
        lv_timer_delete(s_timer);
        s_timer = NULL;
    }
    lv_obj_remove_flag(s_splash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_fade_out(s_splash, 300, 0);
    lv_obj_delete_delayed(s_splash, 320);
}

static void timer_cb(lv_timer_t *t)
{
    (void)t;
    s_timer = NULL;   /* one-shot: LVGL deletes it after this call */
    ui_hide_splash();
}

static void tap_cb(lv_event_t *e)
{
    (void)e;
    ui_hide_splash();
}

/* Text splash for any owner other than the default. */
static void build_named(lv_obj_t *sp)
{
    lv_obj_t *ring = ui_box(sp);
    lv_obj_set_size(ring, 168, 168);
    lv_obj_align(ring, LV_ALIGN_TOP_MID, 0, 96);
    lv_obj_set_style_radius(ring, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(ring, 3, 0);
    lv_obj_set_style_border_color(ring, ui_c(UI_C_ACCENT), 0);
    lv_obj_set_style_bg_color(ring, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_bg_opa(ring, LV_OPA_COVER, 0);

    lv_obj_t *glass = ui_label(ring, &adb_font_28, UI_C_ACCENT, UI_SYM_MARTINI);
    lv_obj_center(glass);
    lv_obj_set_style_transform_pivot_x(glass, LV_PCT(50), 0);
    lv_obj_set_style_transform_pivot_y(glass, LV_PCT(50), 0);
    lv_obj_set_style_transform_scale(glass, 3 * 256, 0);

    char name[ADB_OWNER_MAX + 4];
    adb_owner_possessive(&g_ui.st->settings, name, sizeof name, false);
    lv_obj_t *who = ui_label(sp, &adb_font_28, UI_C_ACCENT, name);
    lv_obj_set_width(who, UI_HOR_RES - 32);
    lv_label_set_long_mode(who, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(who, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(who, LV_ALIGN_TOP_MID, 0, 290);

    lv_obj_t *title = ui_label(sp, &adb_font_24, UI_C_ACCENT2, "DRINK BUTLER");
    lv_obj_set_style_text_letter_space(title, 3, 0);
    lv_obj_align_to(title, who, LV_ALIGN_OUT_BOTTOM_MID, 0, 6);

    lv_obj_t *rule = ui_box(sp);
    lv_obj_set_size(rule, 120, 2);
    lv_obj_set_style_bg_color(rule, ui_c(UI_C_ACCENT), 0);
    lv_obj_set_style_bg_opa(rule, LV_OPA_COVER, 0);
    lv_obj_align_to(rule, title, LV_ALIGN_OUT_BOTTOM_MID, 0, 12);
}

void ui_show_splash(uint32_t ms)
{
    if (s_splash) lv_obj_delete(s_splash);
    adb_settings_t *set = &g_ui.st->settings;

    s_splash = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_splash);
    lv_obj_set_size(s_splash, UI_HOR_RES, UI_VER_RES);
    lv_obj_set_style_bg_color(s_splash, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(s_splash, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_splash, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_splash, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_splash, tap_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_splash, splash_deleted_cb, LV_EVENT_DELETE, NULL);

    if (adb_owner_is_default(set)) {
        /* The logos are RGB565 with UI_C_BG baked in; use the same rounded
         * colour so no square shows around them on 24-bit displays. */
        lv_obj_set_style_bg_color(s_splash, ui_c(UI_C_BG & 0xF8FCF8), 0);
        lv_obj_t *img = lv_image_create(s_splash);
        lv_image_set_src(img, k_logos[set->next_logo % UI_LOGO_COUNT]);
        lv_obj_align(img, LV_ALIGN_TOP_MID, 0, 84);
        set->next_logo = (uint8_t)((set->next_logo + 1) % UI_LOGO_COUNT);
        ui_save_settings();
    } else {
        build_named(s_splash);
    }

    lv_obj_t *hint = ui_label(s_splash, &adb_font_14, UI_C_MUTED, "Tap to start");
    lv_obj_align(hint, LV_ALIGN_BOTTOM_MID, 0, -28);

    if (ms) {
        s_timer = lv_timer_create(timer_cb, ms, NULL);
        lv_timer_set_repeat_count(s_timer, 1);
    }
}

bool ui_splash_visible(void) { return s_splash != NULL; }
