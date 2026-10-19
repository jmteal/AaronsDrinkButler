/* Splash and screensaver. The default owner (Aaron) gets one of the four
 * logos, a different one each time; anyone else gets a text splash with
 * their name. Tapping dismisses it.
 *
 * Screensaver: after UI_SAVER_AFTER_S without a touch the splash comes up
 * and cycles through the logos. ui_sleep_prepare() puts up a fresh logo just
 * before the screen goes off, so that is what shows on wake. Tapping the
 * screensaver or the wake splash goes to the Home tab. */
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
static lv_obj_t *s_logo;          /* logo image, NULL for the text splash */
static lv_timer_t *s_timer;       /* auto-hide */
static lv_timer_t *s_cycle;       /* screensaver logo cycle */
static lv_timer_t *s_swap;        /* mid-fade logo swap */
static lv_timer_t *s_idle;        /* watches for the screensaver timeout */
static bool s_go_home;            /* tapping it goes to the Home tab */
static bool s_logo_dirty;         /* next_logo changed but not saved */

static void del_timer(lv_timer_t **t)
{
    if (*t) {
        lv_timer_delete(*t);
        *t = NULL;
    }
}

static void splash_deleted_cb(lv_event_t *e)
{
    (void)e;
    s_splash = NULL;
    s_logo = NULL;
    del_timer(&s_timer);
    del_timer(&s_cycle);
    del_timer(&s_swap);
}

/* The next logo in turn (and advance the counter). Saved later, not on
 * every screensaver step, to spare the flash. */
static const lv_image_dsc_t *take_logo(void)
{
    adb_settings_t *set = &g_ui.st->settings;
    const lv_image_dsc_t *img = k_logos[set->next_logo % UI_LOGO_COUNT];
    set->next_logo = (uint8_t)((set->next_logo + 1) % UI_LOGO_COUNT);
    s_logo_dirty = true;
    return img;
}

static void save_logo(void)
{
    if (!s_logo_dirty) return;
    s_logo_dirty = false;
    ui_save_settings();
}

void ui_hide_splash(void)
{
    if (!s_splash) return;
    del_timer(&s_timer);
    del_timer(&s_cycle);
    del_timer(&s_swap);
    save_logo();
    if (s_go_home) {
        s_go_home = false;
        ui_close_overlays();
        ui_show_tab(UI_TAB_HOME);
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
    s_go_home = false;

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
        s_logo = lv_image_create(s_splash);
        lv_image_set_src(s_logo, take_logo());
        lv_obj_align(s_logo, LV_ALIGN_TOP_MID, 0, 84);
        if (ms) save_logo();   /* a timed splash is a startup/preview: save now */
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

/* ---- Screensaver ------------------------------------------------------ */

#define SWAP_FADE_MS 400

static void swap_cb(lv_timer_t *t)
{
    (void)t;
    s_swap = NULL;   /* one-shot */
    if (!s_logo) return;
    lv_image_set_src(s_logo, take_logo());
    lv_obj_fade_in(s_logo, SWAP_FADE_MS, 0);
}

static void cycle_cb(lv_timer_t *t)
{
    (void)t;
    if (!s_logo || s_swap) return;
    lv_obj_fade_out(s_logo, SWAP_FADE_MS, 0);
    s_swap = lv_timer_create(swap_cb, SWAP_FADE_MS + 20, NULL);
    lv_timer_set_repeat_count(s_swap, 1);
}

static void saver_start(void)
{
    if (!s_splash) ui_show_splash(0);
    s_go_home = true;
    del_timer(&s_timer);
    if (s_logo && !s_cycle) s_cycle = lv_timer_create(cycle_cb, UI_SAVER_CYCLE_MS, NULL);
}

static void idle_cb(lv_timer_t *t)
{
    (void)t;
    if (!g_ui.st->settings.sleep_s || s_cycle) return;   /* "Never", or already running */
    if (lv_display_get_inactive_time(NULL) >= UI_SAVER_AFTER_S * 1000u) saver_start();
}

void ui_saver_init(void)
{
    if (!s_idle) s_idle = lv_timer_create(idle_cb, 1000, NULL);
}

void ui_sleep_prepare(void)
{
    del_timer(&s_cycle);
    del_timer(&s_swap);
    if (!g_ui.st->settings.splash_on) {
        /* No logo wanted on wake: drop straight back to Home */
        if (s_splash) lv_obj_delete(s_splash);
        ui_close_overlays();
        ui_show_tab(UI_TAB_HOME);
        return;
    }
    ui_show_splash(0);   /* a fresh logo, shown as soon as the screen wakes */
    save_logo();
    s_go_home = true;
}
