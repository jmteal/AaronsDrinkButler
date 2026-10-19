/* Home: seasonal "drink of the moment" card, four big shortcuts, search. */
#include <stdio.h>
#include <stdlib.h>
#include "ui_internal.h"

static lv_obj_t *s_card_img, *s_card_kicker, *s_card_name, *s_card_sub, *s_who;
static int32_t s_card_recipe = -1;

/* Pick a drink for the current season; changes every hour. */
static int32_t pick_drink_of_the_moment(void)
{
    const adb_db_t *db = g_ui.db;
    if (!db->n_recipes) return -1;
    int season = ui_current_season();
    uint32_t want = 1u << season;
    if (season == ADB_SEASON_HOLIDAY) want |= 1u << ADB_SEASON_WINTER;
    uint16_t cand[ADB_MAX_RECIPES];
    int n = 0;
    for (uint16_t i = 0; i < db->n_recipes; i++)
        if (db->recipes[i].season_mask & want) cand[n++] = i;
    if (!n) for (uint16_t i = 0; i < db->n_recipes; i++) cand[n++] = i;
    struct tm t;
    uint32_t slot = ui_now(&t) ? (uint32_t)(t.tm_yday * 24 + t.tm_hour) : 0;
    return cand[slot % (uint32_t)n];
}

void ui_home_refresh(void)
{
    s_card_recipe = pick_drink_of_the_moment();
    if (s_card_recipe < 0) {
        lv_label_set_text(s_card_name, "No drinks loaded");
        return;
    }
    const adb_recipe_t *r = &g_ui.db->recipes[s_card_recipe];
    int season = ui_current_season();
    lv_label_set_text(s_card_kicker, "DRINK OF THE MOMENT");
    lv_label_set_text(s_card_name, r->name);
    char sub[96];
    if (season == ADB_SEASON_ALL_YEAR) snprintf(sub, sizeof sub, "%s", adb_recipe_spirit_label(r));
    else snprintf(sub, sizeof sub, "%s  \xE2\x80\xA2  %s pick", adb_recipe_spirit_label(r), adb_tag_label(ADB_TAG_SEASON, season));
    lv_label_set_text(s_card_sub, sub);
    lv_image_set_src(s_card_img, ui_recipe_thumb((uint16_t)s_card_recipe));
}

static void card_cb(lv_event_t *e)
{
    (void)e;
    if (s_card_recipe >= 0) {
        ui_sound(UI_SOUND_TAP);
        ui_open_detail((uint16_t)s_card_recipe);
    }
}

enum { ACT_MAKE, ACT_SURPRISE, ACT_SEASONAL, ACT_MOCKTAILS };

static void action_cb(lv_event_t *e)
{
    int act = (int)(intptr_t)lv_event_get_user_data(e);
    adb_filter_t *f = &g_ui.browse_filter;
    switch (act) {
    case ACT_MAKE:
        ui_sound(UI_SOUND_TAP);
        ui_show_tab(UI_TAB_MYBAR);
        ui_mybar_show(true, 0);
        break;
    case ACT_SURPRISE:
        if (g_ui.db->n_recipes) {
            ui_sound(UI_SOUND_CLINK);
            ui_open_detail((uint16_t)(rand() % g_ui.db->n_recipes));
        }
        break;
    case ACT_SEASONAL: {
        ui_sound(UI_SOUND_TAP);
        adb_filter_clear(f);
        int s = ui_current_season();
        adb_filter_set_tag(f, ADB_TAG_SEASON, s, true);
        if (s == ADB_SEASON_HOLIDAY) adb_filter_set_tag(f, ADB_TAG_SEASON, ADB_SEASON_WINTER, true);
        ui_show_tab(UI_TAB_BROWSE);
        break;
    }
    case ACT_MOCKTAILS:
        ui_sound(UI_SOUND_TAP);
        adb_filter_clear(f);
        adb_filter_set_tag(f, ADB_TAG_ALCOHOLIC, ADB_ALC_MOCKTAIL, true);
        ui_show_tab(UI_TAB_BROWSE);
        break;
    }
}

static void search_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_TAP);
    ui_open_search("");
}

static void settings_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_TAP);
    ui_open_settings();
}

void ui_home_set_owner(const char *possessive)
{
    if (s_who) lv_label_set_text_fmt(s_who, "%s bar", possessive);
}

void ui_home_create(lv_obj_t *page)
{
    lv_obj_set_style_pad_all(page, 0, 0);

    /* title row */
    lv_obj_t *title = ui_label(page, &adb_font_24, UI_C_ACCENT, "Drink Butler");
    lv_obj_set_pos(title, 16, 14);
    lv_obj_update_layout(title);
    s_who = ui_label(page, &adb_font_14, UI_C_MUTED, "");
    /* room between the title and the gear button */
    lv_obj_set_size(s_who, UI_HOR_RES - UI_TOUCH - 10 - 8 - (16 + lv_obj_get_width(title) + 8),
                    lv_font_get_line_height(&adb_font_14));
    lv_label_set_long_mode(s_who, LV_LABEL_LONG_DOT);
    lv_obj_align_to(s_who, title, LV_ALIGN_OUT_RIGHT_BOTTOM, 8, -3);
    lv_obj_t *gear = ui_icon_button(page, LV_SYMBOL_SETTINGS, UI_TOUCH);
    lv_obj_set_pos(gear, UI_HOR_RES - UI_TOUCH - 10, 6);
    lv_obj_add_event_cb(gear, settings_cb, LV_EVENT_CLICKED, NULL);

    /* drink of the moment card */
    lv_obj_t *card = ui_button(page, NULL, false);
    lv_obj_set_size(card, UI_HOR_RES - 24, 120);
    lv_obj_set_pos(card, 12, 58);
    lv_obj_set_style_bg_color(card, ui_c(UI_C_SURFACE), 0);
    lv_obj_set_style_pad_all(card, 12, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, ui_c(UI_C_DIVIDER), 0);
    lv_obj_add_event_cb(card, card_cb, LV_EVENT_CLICKED, NULL);
    s_card_img = lv_image_create(card);
    lv_obj_set_size(s_card_img, UI_THUMB, UI_THUMB);
    lv_obj_set_style_radius(s_card_img, 10, 0);
    lv_obj_set_style_clip_corner(s_card_img, true, 0);
    lv_obj_align(s_card_img, LV_ALIGN_LEFT_MID, -2, 0);
    int tx = UI_THUMB + 10, tw = UI_HOR_RES - 24 - 24 - UI_THUMB - 10;
    s_card_kicker = ui_label(card, &adb_font_12, UI_C_ACCENT, "");
    lv_obj_set_pos(s_card_kicker, tx, 4);
    lv_obj_set_size(s_card_kicker, tw, 16);
    lv_label_set_long_mode(s_card_kicker, LV_LABEL_LONG_MODE_DOTS);
    s_card_name = ui_label(card, &adb_font_24, UI_C_TEXT, "");
    lv_obj_set_pos(s_card_name, tx, 24);
    lv_obj_set_width(s_card_name, tw);
    lv_label_set_long_mode(s_card_name, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_set_style_max_height(s_card_name, 58, 0);
    s_card_sub = ui_label(card, &adb_font_14, UI_C_MUTED, "");
    lv_obj_set_size(s_card_sub, tw, 18);
    lv_label_set_long_mode(s_card_sub, LV_LABEL_LONG_MODE_DOTS);
    lv_obj_align(s_card_sub, LV_ALIGN_BOTTOM_LEFT, tx, 0);

    /* 2x2 shortcuts */
    static const struct { const char *sym, *text; } acts[] = {
        { UI_SYM_COCKTAIL, "What Can\nI Make?" }, { LV_SYMBOL_SHUFFLE, "Surprise\nMe" },
        { UI_SYM_LEAF, "Seasonal\nPicks" },       { UI_SYM_STAR, "Mocktails" },
    };
    int bw = (UI_HOR_RES - 24 - 10) / 2, bh = 68;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *b = ui_button(page, NULL, i == 0);
        lv_obj_set_size(b, bw, bh);
        lv_obj_set_pos(b, 12 + (i % 2) * (bw + 10), 190 + (i / 2) * (bh + 10));
        lv_obj_t *ic = lv_label_create(b);
        lv_obj_set_style_text_font(ic, &adb_font_24, 0);
        if (i) lv_obj_set_style_text_color(ic, ui_c(UI_C_ACCENT), 0);
        lv_label_set_text(ic, acts[i].sym);
        lv_obj_align(ic, LV_ALIGN_LEFT_MID, 2, 0);
        lv_obj_t *l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &adb_font_16, 0);
        lv_label_set_text(l, acts[i].text);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 40, 0);
        lv_obj_add_event_cb(b, action_cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    }

    lv_obj_t *search = ui_search_field(page, NULL);
    lv_obj_set_size(search, UI_HOR_RES - 24, UI_TOUCH + 4);
    lv_obj_set_pos(search, 12, 346);
    lv_obj_add_event_cb(search, search_cb, LV_EVENT_CLICKED, NULL);
}
