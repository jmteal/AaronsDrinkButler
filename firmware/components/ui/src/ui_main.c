/* App shell: status bar, bottom tab bar, page switching and overlays. */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

ui_ctx_t g_ui;

static lv_obj_t *s_time_lbl, *s_batt_lbl, *s_mute_lbl, *s_title_lbl;
#define MAX_OVERLAYS 4
static lv_obj_t *s_overlays[MAX_OVERLAYS];
static int s_n_overlays;

/* ---- Platform wrappers ------------------------------------------------ */

void ui_sound(ui_sound_t s)
{
    if (g_ui.st->settings.sound_on && g_ui.plat && g_ui.plat->play_sound) g_ui.plat->play_sound(g_ui.plat->ctx, s);
}

void ui_save_favorites(void) { adb_state_save_favorites(g_ui.st, g_ui.db, g_ui.io); }
void ui_save_pantry(void)    { adb_state_save_pantry(g_ui.st, g_ui.db, g_ui.io); }
void ui_save_settings(void)  { adb_state_save_settings(g_ui.st, g_ui.io); }

bool ui_now(struct tm *out)
{
    return g_ui.plat && g_ui.plat->get_time && g_ui.plat->get_time(g_ui.plat->ctx, out);
}

int ui_current_season(void)
{
    struct tm t;
    if (!ui_now(&t)) return ADB_SEASON_ALL_YEAR;
    int m = t.tm_mon; /* 0 = January */
    if (m == 11) return ADB_SEASON_HOLIDAY;
    if (m <= 1) return ADB_SEASON_WINTER;
    if (m <= 4) return ADB_SEASON_SPRING;
    if (m <= 7) return ADB_SEASON_SUMMER;
    return ADB_SEASON_FALL;
}

/* ---- Status bar ------------------------------------------------------- */

/* Centre the title in whatever room the clock and battery leave. */
static void fit_title(void)
{
    lv_obj_update_layout(g_ui.status_bar);
    lv_area_t a;
    lv_obj_get_coords(s_time_lbl, &a);
    int32_t left = (lv_obj_get_width(s_time_lbl) ? a.x2 : a.x1) + 8;
    lv_obj_get_coords(lv_obj_get_parent(s_batt_lbl), &a);
    int32_t right = a.x1 - 8, mid = UI_HOR_RES / 2;
    int32_t half = LV_MIN(mid - left, right - mid);
    lv_obj_set_width(s_title_lbl, half > 0 ? 2 * half : 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_CENTER, 0, 0);
}

void ui_refresh_status(void)
{
    struct tm t;
    if (ui_now(&t)) {
        int h = t.tm_hour % 12;
        lv_label_set_text_fmt(s_time_lbl, "%d:%02d", h ? h : 12, t.tm_min);
    } else {
        lv_label_set_text(s_time_lbl, "");
    }
    int pct = (g_ui.plat && g_ui.plat->battery_percent) ? g_ui.plat->battery_percent(g_ui.plat->ctx) : -1;
    bool chg = g_ui.plat && g_ui.plat->is_charging && g_ui.plat->is_charging(g_ui.plat->ctx);
    if (pct < 0) {
        lv_label_set_text(s_batt_lbl, chg ? LV_SYMBOL_CHARGE : "");
    } else {
        const char *sym = pct > 85 ? LV_SYMBOL_BATTERY_FULL : pct > 60 ? LV_SYMBOL_BATTERY_3
                        : pct > 35 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
        lv_label_set_text_fmt(s_batt_lbl, "%s%d%% %s", chg ? LV_SYMBOL_CHARGE " " : "", pct, sym);
        lv_obj_set_style_text_color(s_batt_lbl, ui_c(pct <= 10 && !chg ? UI_C_BAD : UI_C_TEXT), 0);
    }
    lv_label_set_text(s_mute_lbl, g_ui.st->settings.sound_on ? "" : LV_SYMBOL_MUTE);
    fit_title();
}

static void status_timer_cb(lv_timer_t *t)
{
    (void)t;
    ui_refresh_status();
}

static void build_status_bar(void)
{
    lv_obj_t *sb = ui_box(g_ui.root);
    g_ui.status_bar = sb;
    lv_obj_set_size(sb, UI_HOR_RES, UI_STATUS_H);
    lv_obj_set_style_bg_color(sb, ui_c(UI_C_STATUS), 0);
    lv_obj_set_style_bg_opa(sb, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(sb, 10, 0);

    s_time_lbl = ui_label(sb, &adb_font_14, UI_C_TEXT, "");
    lv_obj_align(s_time_lbl, LV_ALIGN_LEFT_MID, 0, 0);
    s_title_lbl = ui_label(sb, &adb_font_12, UI_C_MUTED, "");
    lv_obj_set_height(s_title_lbl, lv_font_get_line_height(&adb_font_12));
    lv_label_set_long_mode(s_title_lbl, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_align(s_title_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_align(s_title_lbl, LV_ALIGN_CENTER, 0, 0);
    lv_obj_t *right = ui_box(sb);
    lv_obj_set_size(right, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(right, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(right, 8, 0);
    lv_obj_align(right, LV_ALIGN_RIGHT_MID, 0, 0);
    s_mute_lbl = ui_label(right, &adb_font_14, UI_C_MUTED, "");
    s_batt_lbl = ui_label(right, &adb_font_14, UI_C_TEXT, "");
    lv_timer_create(status_timer_cb, 10000, NULL);
}

/* ---- Tab bar ---------------------------------------------------------- */

static const char *k_tab_sym[UI_TAB_COUNT] = { LV_SYMBOL_HOME, LV_SYMBOL_LIST, UI_SYM_COCKTAIL, UI_SYM_HEART };
static const char *k_tab_txt[UI_TAB_COUNT] = { "Home", "Browse", "My Bar", "Favorites" };

static void tab_cb(lv_event_t *e)
{
    ui_tab_t tab = (ui_tab_t)(intptr_t)lv_event_get_user_data(e);
    ui_sound(UI_SOUND_TAP);
    ui_close_overlays();
    ui_show_tab(tab);
}

static void build_tabbar(void)
{
    lv_obj_t *tb = ui_box(g_ui.root);
    g_ui.tabbar = tb;
    lv_obj_set_size(tb, UI_HOR_RES, UI_TABBAR_H);
    lv_obj_set_pos(tb, 0, UI_VER_RES - UI_TABBAR_H);
    lv_obj_set_style_bg_color(tb, ui_c(UI_C_STATUS), 0);
    lv_obj_set_style_bg_opa(tb, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(tb, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(tb, 1, 0);
    lv_obj_set_style_border_color(tb, ui_c(UI_C_DIVIDER), 0);
    lv_obj_set_flex_flow(tb, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        lv_obj_t *b = lv_button_create(tb);
        lv_obj_remove_style_all(b);
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_style_text_color(b, ui_c(UI_C_MUTED), 0);
        lv_obj_set_style_text_color(b, ui_c(UI_C_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(b, ui_c(UI_C_SURFACE), LV_STATE_PRESSED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
        lv_obj_t *ic = lv_label_create(b);
        lv_obj_set_style_text_font(ic, &adb_font_20, 0);
        lv_label_set_text(ic, k_tab_sym[i]);
        lv_obj_align(ic, LV_ALIGN_TOP_MID, 0, 7);
        lv_obj_t *tx = lv_label_create(b);
        lv_obj_set_style_text_font(tx, &adb_font_12, 0);
        lv_label_set_text(tx, k_tab_txt[i]);
        lv_obj_align(tx, LV_ALIGN_BOTTOM_MID, 0, -6);
        lv_obj_add_event_cb(b, tab_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
        g_ui.tab_btns[i] = b;
    }
}

void ui_show_tab(ui_tab_t tab)
{
    if (tab >= UI_TAB_COUNT) return;
    g_ui.tab = tab;
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        if (i == (int)tab) lv_obj_remove_flag(g_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(g_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
        ui_chip_set_checked(g_ui.tab_btns[i], i == (int)tab);
    }
    /* refresh the page being shown: rows of hidden pages may point at
     * icons that were evicted from the thumbnail cache meanwhile */
    switch (tab) {
    case UI_TAB_HOME: ui_home_refresh(); break;
    case UI_TAB_BROWSE: ui_browse_refresh(); break;
    case UI_TAB_MYBAR: ui_mybar_refresh(); break;
    case UI_TAB_FAVORITES: ui_favorites_refresh(); break;
    default: break;
    }
}

/* ---- Overlays --------------------------------------------------------- */

lv_obj_t *ui_overlay_push(void)
{
    if (s_n_overlays >= MAX_OVERLAYS) ui_overlay_pop();
    lv_obj_t *o = ui_box(g_ui.overlay_layer);
    lv_obj_set_size(o, UI_HOR_RES, UI_OVERLAY_H);
    lv_obj_set_style_bg_color(o, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE); /* block touches to what is below */
    s_overlays[s_n_overlays++] = o;
    lv_obj_remove_flag(g_ui.overlay_layer, LV_OBJ_FLAG_HIDDEN);
    return o;
}

void ui_overlay_pop(void)
{
    if (!s_n_overlays) return;
    lv_obj_delete(s_overlays[--s_n_overlays]);
    if (!s_n_overlays) {
        lv_obj_add_flag(g_ui.overlay_layer, LV_OBJ_FLAG_HIDDEN);
        ui_show_tab(g_ui.tab); /* refresh, e.g. favorites changed on the detail screen */
    }
}

lv_obj_t *ui_overlay_top(void)
{
    return s_n_overlays ? s_overlays[s_n_overlays - 1] : NULL;
}

ui_tab_t ui_current_tab(void) { return g_ui.tab; }

bool ui_overlay_open(void) { return s_n_overlays > 0; }

void ui_close_overlays(void)
{
    while (s_n_overlays) {
        lv_obj_delete(s_overlays[--s_n_overlays]);
    }
    lv_obj_add_flag(g_ui.overlay_layer, LV_OBJ_FLAG_HIDDEN);
}

void ui_open_detail(uint16_t recipe)
{
    if (recipe >= g_ui.db->n_recipes) return;
    ui_detail_create(ui_overlay_push(), recipe);
}
void ui_open_filter(void) { ui_filter_create(ui_overlay_push()); }
void ui_open_search(const char *t) { ui_search_create(ui_overlay_push(), t); }
void ui_open_settings(void) { ui_settings_create(ui_overlay_push()); }
void ui_open_credits(void) { ui_credits_create(ui_overlay_push()); }
void ui_open_owner_editor(void) { ui_owner_create(ui_overlay_push()); }

/* Owner name shown in the status bar title, on Home and in Settings. */
void ui_owner_changed(void)
{
    char poss[ADB_OWNER_MAX + 4], title[ADB_OWNER_MAX + 20];
    adb_owner_possessive(&g_ui.st->settings, poss, sizeof poss, true);
    snprintf(title, sizeof title, "%s DRINK BUTLER", poss);
    lv_label_set_text(s_title_lbl, title);
    fit_title();
    adb_owner_possessive(&g_ui.st->settings, poss, sizeof poss, false);
    ui_home_set_owner(poss);
    ui_settings_set_owner();
}

void ui_scroll_overlay_to(int32_t y)
{
    lv_obj_t *ov = ui_overlay_top();
    if (!ov) return;
    lv_obj_update_layout(ov);
    for (uint32_t i = 0; i < lv_obj_get_child_count(ov); i++) {
        lv_obj_t *c = lv_obj_get_child(ov, (int32_t)i);
        if (!lv_obj_has_flag(c, LV_OBJ_FLAG_SCROLLABLE) || lv_obj_get_scroll_bottom(c) <= 0) continue;
        int32_t max = lv_obj_get_scroll_y(c) + lv_obj_get_scroll_bottom(c);
        lv_obj_scroll_to_y(c, (y < 0 || y > max) ? max : y, LV_ANIM_OFF);
        return;
    }
}

adb_filter_t *ui_browse_filter(void) { return &g_ui.browse_filter; }
void ui_browse_apply_filter(void) { ui_browse_refresh(); }

void ui_on_favorites_changed(void)
{
    ui_save_favorites();
    ui_detail_refresh_heart();
}

void ui_refresh_all(void)
{
    ui_refresh_status();
    ui_show_tab(g_ui.tab);
}

/* ---- Init ------------------------------------------------------------- */

void ui_init(const ui_config_t *cfg)
{
    memset(&g_ui, 0, sizeof(g_ui));
    g_ui.db = cfg->db;
    g_ui.st = cfg->state;
    g_ui.io = cfg->storage;
    g_ui.plat = cfg->platform;
    g_ui.img = cfg->images;
    adb_filter_clear(&g_ui.browse_filter);

    ui_theme_init();
    if (g_ui.plat) {
        const adb_settings_t *s = &g_ui.st->settings;
        if (g_ui.plat->set_brightness) g_ui.plat->set_brightness(g_ui.plat->ctx, s->brightness);
        if (g_ui.plat->set_sleep_timeout) g_ui.plat->set_sleep_timeout(g_ui.plat->ctx, s->sleep_s);
        if (g_ui.plat->set_sound) g_ui.plat->set_sound(g_ui.plat->ctx, s->sound_on, s->volume);
    }

    g_ui.root = lv_screen_active();
    lv_obj_clean(g_ui.root);
    lv_obj_remove_flag(g_ui.root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(g_ui.root, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(g_ui.root, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(g_ui.root, ui_c(UI_C_TEXT), 0);
    lv_obj_set_style_text_font(g_ui.root, &adb_font_16, 0);

    build_status_bar();

    g_ui.content = ui_box(g_ui.root);
    lv_obj_set_size(g_ui.content, UI_HOR_RES, UI_CONTENT_H);
    lv_obj_set_pos(g_ui.content, 0, UI_STATUS_H);
    for (int i = 0; i < UI_TAB_COUNT; i++) {
        g_ui.pages[i] = ui_box(g_ui.content);
        lv_obj_set_size(g_ui.pages[i], UI_HOR_RES, UI_CONTENT_H);
        lv_obj_set_style_bg_color(g_ui.pages[i], ui_c(UI_C_BG), 0);
        lv_obj_set_style_bg_opa(g_ui.pages[i], LV_OPA_COVER, 0);
        lv_obj_add_flag(g_ui.pages[i], LV_OBJ_FLAG_HIDDEN);
    }
    build_tabbar();

    g_ui.overlay_layer = ui_box(g_ui.root);
    lv_obj_set_size(g_ui.overlay_layer, UI_HOR_RES, UI_OVERLAY_H);
    lv_obj_set_pos(g_ui.overlay_layer, 0, UI_STATUS_H);
    lv_obj_add_flag(g_ui.overlay_layer, LV_OBJ_FLAG_HIDDEN);

    ui_home_create(g_ui.pages[UI_TAB_HOME]);
    ui_browse_create(g_ui.pages[UI_TAB_BROWSE]);
    ui_mybar_create(g_ui.pages[UI_TAB_MYBAR]);
    ui_favorites_create(g_ui.pages[UI_TAB_FAVORITES]);

    ui_owner_changed();
    ui_refresh_status();
    ui_show_tab(UI_TAB_HOME);
    if (cfg->splash_ms && g_ui.st->settings.splash_on)
        ui_show_splash(cfg->splash_ms == UI_SPLASH_UNTIL_TAP ? 0 : cfg->splash_ms);
    ui_saver_init();
}
