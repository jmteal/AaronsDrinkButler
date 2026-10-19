/* Settings (owner, splash, brightness, sleep, sound, units, battery, credits, version)
 * and the Credits / attribution page. */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

/* Screen-off times. The logo screensaver starts at UI_SAVER_AFTER_S. */
static const uint16_t k_sleep[] = { 300, 600, 900, 1800, 0 };
static const char *const k_sleep_txt[] = { "5 min", "10 min", "15 min", "30 min", "Never" };
#define N_SLEEP 5

static lv_obj_t *s_batt_val, *s_owner_val;

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_BACK);
    ui_overlay_pop();
}

static lv_obj_t *card(lv_obj_t *parent)
{
    lv_obj_t *c = ui_box(parent);
    lv_obj_set_size(c, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_style_bg_color(c, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, 12, 0);
    lv_obj_set_style_pad_all(c, 12, 0);
    lv_obj_set_style_pad_row(c, 8, 0);
    lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
    return c;
}

static lv_obj_t *row(lv_obj_t *parent, const char *sym, const char *title)
{
    lv_obj_t *r = ui_box(parent);
    lv_obj_set_size(r, LV_PCT(100), UI_TOUCH);
    char buf[64];
    snprintf(buf, sizeof buf, "%s   %s", sym, title);
    lv_obj_t *l = ui_label(r, &adb_font_16, UI_C_TEXT, buf);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    return r;
}

static void style_slider(lv_obj_t *s)
{
    lv_obj_set_style_bg_color(s, ui_c(UI_C_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s, ui_c(UI_C_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s, ui_c(UI_C_TEXT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s, 6, LV_PART_KNOB);
    lv_obj_set_style_shadow_width(s, 0, LV_PART_KNOB);
    lv_obj_set_ext_click_area(s, 16);
}

static void brightness_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_current_target_obj(e);
    g_ui.st->settings.brightness = (uint8_t)lv_slider_get_value(s);
    if (g_ui.plat && g_ui.plat->set_brightness) g_ui.plat->set_brightness(g_ui.plat->ctx, g_ui.st->settings.brightness);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) ui_save_settings();
}

static void apply_sound(void)
{
    if (g_ui.plat && g_ui.plat->set_sound)
        g_ui.plat->set_sound(g_ui.plat->ctx, g_ui.st->settings.sound_on, g_ui.st->settings.volume);
}

static void volume_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_current_target_obj(e);
    g_ui.st->settings.volume = (uint8_t)lv_slider_get_value(s);
    apply_sound();
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        ui_save_settings();
        ui_sound(UI_SOUND_TAP);
    }
}

static void sound_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_current_target_obj(e);
    g_ui.st->settings.sound_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    apply_sound();
    ui_save_settings();
    ui_refresh_status();
    ui_sound(UI_SOUND_TOGGLE);
}

static void sleep_cb(lv_event_t *e)
{
    int i = ui_segmented_get(lv_event_get_current_target_obj(e));
    g_ui.st->settings.sleep_s = k_sleep[i];
    if (g_ui.plat && g_ui.plat->set_sleep_timeout) g_ui.plat->set_sleep_timeout(g_ui.plat->ctx, k_sleep[i]);
    ui_save_settings();
}

static void units_cb(lv_event_t *e)
{
    g_ui.st->settings.use_oz = ui_segmented_get(lv_event_get_current_target_obj(e)) == 0;
    ui_save_settings();
}

static void owner_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_TAP);
    ui_open_owner_editor();
}

static void splash_cb(lv_event_t *e)
{
    lv_obj_t *sw = lv_event_get_current_target_obj(e);
    g_ui.st->settings.splash_on = lv_obj_has_state(sw, LV_STATE_CHECKED);
    ui_save_settings();
    ui_sound(UI_SOUND_TOGGLE);
}

static void preview_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_TAP);
    ui_show_splash(2500);
}

static void owner_val_deleted_cb(lv_event_t *e)
{
    (void)e;
    s_owner_val = NULL;
}

void ui_settings_set_owner(void)
{
    if (!s_owner_val) return;
    const adb_settings_t *st = &g_ui.st->settings;
    lv_label_set_text(s_owner_val, adb_owner_name(st));
}

static lv_obj_t *settings_switch(lv_obj_t *r, bool on, lv_event_cb_t cb)
{
    lv_obj_t *sw = lv_switch_create(r);
    lv_obj_set_size(sw, 56, 30);
    lv_obj_align(sw, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_ext_click_area(sw, 8);
    lv_obj_set_style_bg_color(sw, ui_c(UI_C_SURFACE2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sw, ui_c(UI_C_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    lv_obj_set_style_bg_opa(sw, LV_OPA_COVER, LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return sw;
}

/* Full-width tappable row: icon + title on the left, value + chevron on the right. */
static lv_obj_t *nav_row(lv_obj_t *parent, const char *sym, const char *title, lv_obj_t **value_out,
                         lv_event_cb_t cb)
{
    lv_obj_t *b = ui_button(parent, NULL, false);
    lv_obj_set_size(b, LV_PCT(100), UI_TOUCH);
    lv_obj_set_style_bg_color(b, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_pad_hor(b, 0, 0);
    char buf[64];
    snprintf(buf, sizeof buf, "%s   %s", sym, title);
    lv_obj_t *l = ui_label(b, &adb_font_16, UI_C_TEXT, buf);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_t *chev = ui_label(b, &adb_font_14, UI_C_MUTED, LV_SYMBOL_RIGHT);
    lv_obj_align(chev, LV_ALIGN_RIGHT_MID, 0, 0);
    if (value_out) {
        lv_obj_t *v = ui_label(b, &adb_font_14, UI_C_ACCENT, "");
        lv_obj_set_width(v, 110);
        lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
        lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
        lv_obj_align(v, LV_ALIGN_RIGHT_MID, -22, 0);
        *value_out = v;
    }
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
    return b;
}

static void credits_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_TAP);
    ui_open_credits();
}

static void update_battery(void)
{
    const ui_platform_t *p = g_ui.plat;
    int pct = p && p->battery_percent ? p->battery_percent(p->ctx) : -1;
    int mv = p && p->battery_mv ? p->battery_mv(p->ctx) : -1;
    bool chg = p && p->is_charging && p->is_charging(p->ctx);
    char buf[64];
    if (pct < 0) snprintf(buf, sizeof buf, "Not available");
    else if (mv > 0) snprintf(buf, sizeof buf, "%d%%  \xE2\x80\xA2  %d.%02d V%s", pct, mv / 1000, (mv % 1000) / 10, chg ? "  \xE2\x80\xA2  Charging" : "");
    else snprintf(buf, sizeof buf, "%d%%%s", pct, chg ? "  \xE2\x80\xA2  Charging" : "");
    lv_label_set_text(s_batt_val, buf);
}

static lv_obj_t *section(lv_obj_t *body, const char *t)
{
    lv_obj_t *l = ui_label(body, &adb_font_12, UI_C_MUTED, t);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_style_pad_top(l, 8, 0);
    return l;
}

static lv_obj_t *scroll_body(lv_obj_t *ov)
{
    lv_obj_t *body = lv_obj_create(ov);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, UI_HOR_RES, UI_OVERLAY_H - 52);
    lv_obj_set_pos(body, 0, 52);
    lv_obj_set_style_bg_color(body, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_all(body, 12, 0);
    lv_obj_set_style_pad_row(body, 8, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_style_width(body, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(body, ui_c(UI_C_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_SCROLLBAR);
    return body;
}

void ui_settings_create(lv_obj_t *ov)
{
    const adb_settings_t *st = &g_ui.st->settings;
    lv_obj_t *h = ui_header(ov, "Settings", back_cb);
    lv_obj_set_pos(h, 0, 0);
    lv_obj_t *body = scroll_body(ov);

    section(body, "OWNER");
    lv_obj_t *c = card(body);
    nav_row(c, LV_SYMBOL_EDIT, "Owner name", &s_owner_val, owner_cb);
    lv_obj_add_event_cb(s_owner_val, owner_val_deleted_cb, LV_EVENT_DELETE, NULL);
    ui_settings_set_owner();
    lv_obj_t *r = row(c, LV_SYMBOL_IMAGE, "Logo on start/wake");
    settings_switch(r, st->splash_on, splash_cb);
    nav_row(c, LV_SYMBOL_PLAY, "Show splash now", NULL, preview_cb);

    section(body, "SCREEN");
    c = card(body);
    row(c, UI_SYM_SUN, "Brightness");
    lv_obj_t *s = lv_slider_create(c);
    lv_obj_set_width(s, LV_PCT(94));
    lv_obj_set_style_margin_left(s, 8, 0);
    lv_slider_set_range(s, 5, 100);
    lv_slider_set_value(s, st->brightness, LV_ANIM_OFF);
    style_slider(s);
    lv_obj_add_event_cb(s, brightness_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, brightness_cb, LV_EVENT_RELEASED, NULL);
    lv_obj_t *gap = ui_box(c);
    lv_obj_set_size(gap, 1, 4);
    row(c, UI_SYM_MOON, "Screen off after");
    int sel = N_SLEEP - 1;
    for (int i = 0; i < N_SLEEP; i++) if (k_sleep[i] == st->sleep_s) sel = i;
    lv_obj_t *seg = ui_segmented(c, k_sleep_txt, N_SLEEP, sel, sleep_cb, NULL);
    lv_obj_set_width(seg, LV_PCT(100));

    section(body, "SOUND");
    c = card(body);
    r = row(c, LV_SYMBOL_VOLUME_MAX, "Sound effects");
    settings_switch(r, st->sound_on, sound_cb);
    lv_obj_t *vl = ui_label(c, &adb_font_14, UI_C_MUTED, "Volume");
    (void)vl;
    s = lv_slider_create(c);
    lv_obj_set_width(s, LV_PCT(94));
    lv_obj_set_style_margin_left(s, 8, 0);
    lv_obj_set_style_margin_bottom(s, 8, 0);
    lv_slider_set_range(s, 0, 100);
    lv_slider_set_value(s, st->volume, LV_ANIM_OFF);
    style_slider(s);
    lv_obj_add_event_cb(s, volume_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s, volume_cb, LV_EVENT_RELEASED, NULL);

    section(body, "RECIPES");
    c = card(body);
    r = row(c, UI_SYM_MARTINI, "Units");
    static const char *const units[] = { "oz", "ml" };
    seg = ui_segmented(r, units, 2, st->use_oz ? 0 : 1, units_cb, NULL);
    lv_obj_set_width(seg, 120);
    lv_obj_align(seg, LV_ALIGN_RIGHT_MID, 0, 0);

    section(body, "BATTERY");
    c = card(body);
    r = row(c, LV_SYMBOL_BATTERY_FULL, "Battery");
    s_batt_val = ui_label(r, &adb_font_14, UI_C_MUTED, "");
    lv_obj_align(s_batt_val, LV_ALIGN_RIGHT_MID, 0, 0);
    update_battery();

    section(body, "ABOUT");
    c = card(body);
    nav_row(c, UI_SYM_INFO, "Credits & photo licenses", NULL, credits_cb);
    char ver[96];
    snprintf(ver, sizeof ver, "Aaron's Drink Butler %s\n%u drinks  \xE2\x80\xA2  %u ingredients",
             g_ui.plat && g_ui.plat->version ? g_ui.plat->version : "",
             g_ui.db->n_recipes, g_ui.db->n_ingredients);
    ui_label(c, &adb_font_14, UI_C_MUTED, ver);
}

/* ---- Credits ------------------------------------------------------------ */

void ui_credits_create(lv_obj_t *ov)
{
    lv_obj_t *h = ui_header(ov, "Credits", back_cb);
    lv_obj_set_pos(h, 0, 0);
    lv_obj_t *body = scroll_body(ov);
    lv_obj_set_style_pad_row(body, 6, 0);

    static const char *const intro =
        "Recipe specs are facts cross-checked against the IBA official cocktail list, "
        "Wikipedia/Wikidata and TheCocktailDB (thecocktaildb.com). Instructions are written "
        "by us and licensed CC BY 4.0. Code: MIT.";
    lv_obj_t *l = ui_label(body, &adb_font_14, UI_C_TEXT, intro);
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);

    section(body, "PHOTOS");
    int photos = 0;
    for (int i = 0; i < g_ui.db->n_recipes; i++) {
        const adb_recipe_t *r = &g_ui.db->recipes[i];
        if (!r->image.is_photo) continue;
        photos++;
        char buf[320];
        snprintf(buf, sizeof buf, "%s: %s, %s%s", r->name, r->image.author ? r->image.author : "unknown author",
                 r->image.license ? r->image.license : "license unknown", r->image.modified ? ", resized" : "");
        lv_obj_t *p = ui_label(body, &adb_font_12, UI_C_MUTED, buf);
        lv_obj_set_width(p, LV_PCT(100));
        lv_label_set_long_mode(p, LV_LABEL_LONG_MODE_WRAP);
    }
    if (!photos) {
        lv_obj_t *p = ui_label(body, &adb_font_14, UI_C_MUTED,
                               "No photos in this dataset yet. Drink images are generated glass icons.");
        lv_obj_set_width(p, LV_PCT(100));
        lv_label_set_long_mode(p, LV_LABEL_LONG_MODE_WRAP);
    }
    section(body, "FULL LIST");
    l = ui_label(body, &adb_font_14, UI_C_MUTED,
                 "See ATTRIBUTION.md in the project repository (github.com/jmteal/AaronsDrinkButler).");
    lv_obj_set_width(l, LV_PCT(100));
    lv_label_set_long_mode(l, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *fonts = ui_label(body, &adb_font_12, UI_C_MUTED,
                               "Fonts: Montserrat (SIL OFL), Font Awesome 5 Free (SIL OFL / CC BY 4.0). Built with LVGL.");
    lv_obj_set_width(fonts, LV_PCT(100));
    lv_label_set_long_mode(fonts, LV_LABEL_LONG_MODE_WRAP);
}
