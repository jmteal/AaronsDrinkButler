/* Drink detail: 320x240 photo with back, speak and favorite buttons, then a
 * scrolling section with ingredients (oz/ml), steps, glass, garnish, credit.
 * The speaker button reads the recipe aloud (platform speech_*); it shows a
 * stop icon while playing and is hidden for drinks without a clip. */
#include <stdio.h>
#include <string.h>
#include "ui_internal.h"

static lv_obj_t *s_heart, *s_ings_box, *s_overlay, *s_speak_lbl;
static lv_timer_t *s_speak_timer;
static int32_t s_recipe = -1;

/* ---- speak the recipe ---- */

static bool speaking(void)
{
    return g_ui.plat && g_ui.plat->speech_playing && g_ui.plat->speech_playing(g_ui.plat->ctx);
}

static void speak_refresh(void)
{
    if (!s_speak_lbl) return;
    bool on = speaking();
    lv_label_set_text(s_speak_lbl, on ? LV_SYMBOL_STOP : g_ui.st->settings.sound_on ? LV_SYMBOL_VOLUME_MAX : LV_SYMBOL_MUTE);
    lv_obj_set_style_text_color(s_speak_lbl, ui_c(on ? UI_C_ACCENT : g_ui.st->settings.sound_on ? UI_C_TEXT : UI_C_MUTED), 0);
}

static void speak_timer_cb(lv_timer_t *t)
{
    (void)t;
    speak_refresh();   /* flips the stop icon back when the clip ends */
}

static void speak_hint(const char *text)
{
    lv_obj_t *l = ui_label(s_overlay, &adb_font_14, UI_C_TEXT, text);
    lv_obj_set_style_bg_color(l, ui_c(UI_C_STATUS), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(l, 10, 0);
    lv_obj_set_style_pad_ver(l, 6, 0);
    lv_obj_set_style_radius(l, 8, 0);
    lv_obj_align(l, LV_ALIGN_TOP_MID, 0, 64);
    lv_obj_delete_delayed(l, 2000);
}

static void speak_cb(lv_event_t *e)
{
    (void)e;
    const ui_platform_t *p = g_ui.plat;
    if (speaking()) {
        p->speech_stop(p->ctx);
    } else if (!g_ui.st->settings.sound_on) {
        speak_hint("Sound is off (Settings)");
    } else if (!p->speech_play || !p->speech_play(p->ctx, g_ui.db->recipes[s_recipe].id)) {
        speak_hint("Couldn't play the recipe");
    }
    speak_refresh();
}

static void back_cb(lv_event_t *e)
{
    (void)e;
    ui_sound(UI_SOUND_BACK);
    ui_overlay_pop();
}

void ui_detail_refresh_heart(void)
{
    if (!s_overlay || s_recipe < 0) return;
    bool fav = adb_bit_get(g_ui.st->favorites, (unsigned)s_recipe);
    lv_obj_set_style_text_color(s_heart, ui_c(fav ? UI_C_HEART : UI_C_TEXT), 0);
    lv_obj_set_style_text_opa(s_heart, LV_OPA_COVER, 0);
}

static void heart_cb(lv_event_t *e)
{
    (void)e;
    bool fav = !adb_bit_get(g_ui.st->favorites, (unsigned)s_recipe);
    adb_bit_set(g_ui.st->favorites, (unsigned)s_recipe, fav);
    ui_sound(fav ? UI_SOUND_CLINK : UI_SOUND_TOGGLE);
    ui_on_favorites_changed();
}

static void fill_ingredients(void)
{
    const adb_recipe_t *r = &g_ui.db->recipes[s_recipe];
    lv_obj_clean(s_ings_box);
    for (int i = 0; i < r->n_ings; i++) {
        const adb_recipe_ing_t *ri = &r->ings[i];
        lv_obj_t *row = ui_box(s_ings_box);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row, 5, 0);
        char amt[32];
        adb_format_amount(ri, g_ui.st->settings.use_oz, amt, sizeof amt);
        lv_obj_t *a = ui_label(row, &adb_font_16, UI_C_ACCENT, amt);
        lv_obj_set_width(a, 78);
        lv_obj_t *d = ui_label(row, &adb_font_16, ri->optional ? UI_C_MUTED : UI_C_TEXT, "");
        if (ri->optional) lv_label_set_text_fmt(d, "%s (optional)", ri->display);
        else lv_label_set_text(d, ri->display);
        lv_obj_set_width(d, UI_HOR_RES - 32 - 86);
        lv_obj_set_pos(d, 86, 0);
        lv_label_set_long_mode(d, LV_LABEL_LONG_MODE_WRAP);
    }
}

static void units_cb(lv_event_t *e)
{
    lv_obj_t *seg = lv_event_get_current_target_obj(e);
    g_ui.st->settings.use_oz = ui_segmented_get(seg) == 0;
    ui_save_settings();
    fill_ingredients();
}

static void overlay_deleted_cb(lv_event_t *e)
{
    (void)e;
    ui_speech_stop();
    if (s_speak_timer) lv_timer_delete(s_speak_timer);
    s_speak_timer = NULL;
    s_speak_lbl = NULL;
    s_overlay = NULL;
    s_recipe = -1;
}

static lv_obj_t *section_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = ui_label(parent, &adb_font_14, UI_C_MUTED, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_style_pad_top(l, 14, 0);
    lv_obj_set_style_pad_bottom(l, 4, 0);
    return l;
}

void ui_detail_create(lv_obj_t *ov, uint16_t recipe)
{
    s_overlay = ov;
    s_recipe = recipe;
    lv_obj_add_event_cb(ov, overlay_deleted_cb, LV_EVENT_DELETE, NULL);
    const adb_recipe_t *r = &g_ui.db->recipes[recipe];

    /* photo */
    lv_obj_t *photo = lv_image_create(ov);
    lv_obj_set_size(photo, UI_PHOTO_W, UI_PHOTO_H);
    lv_obj_set_pos(photo, 0, 0);
    lv_image_set_src(photo, ui_recipe_photo(recipe));
    lv_image_set_inner_align(photo, LV_IMAGE_ALIGN_COVER);

    lv_obj_t *back = ui_icon_button(ov, LV_SYMBOL_LEFT, UI_TOUCH);
    lv_obj_set_pos(back, 8, 8);
    lv_obj_set_style_bg_color(back, ui_c(UI_C_STATUS), 0);
    lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hb = ui_icon_button(ov, UI_SYM_HEART, 48);
    lv_obj_set_pos(hb, UI_HOR_RES - 48 - 8, 8);
    lv_obj_set_style_bg_color(hb, ui_c(UI_C_STATUS), 0);
    lv_obj_set_style_text_font(hb, &adb_font_24, 0);
    s_heart = lv_obj_get_child(hb, 0);
    lv_obj_add_event_cb(hb, heart_cb, LV_EVENT_CLICKED, NULL);
    ui_detail_refresh_heart();

    const ui_platform_t *p = g_ui.plat;
    if (s_speak_timer) lv_timer_delete(s_speak_timer);   /* a detail opened over another */
    s_speak_timer = NULL;
    s_speak_lbl = NULL;
    if (p && p->speech_available && p->speech_stop && p->speech_available(p->ctx, r->id)) {
        lv_obj_t *sb = ui_icon_button(ov, LV_SYMBOL_VOLUME_MAX, 48);
        lv_obj_set_pos(sb, UI_HOR_RES - 2 * (48 + 8), 8);
        lv_obj_set_style_bg_color(sb, ui_c(UI_C_STATUS), 0);
        lv_obj_set_style_text_font(sb, &adb_font_20, 0);
        s_speak_lbl = lv_obj_get_child(sb, 0);
        lv_obj_add_event_cb(sb, speak_cb, LV_EVENT_CLICKED, NULL);
        s_speak_timer = lv_timer_create(speak_timer_cb, 250, NULL);
        speak_refresh();
    }

    /* scrolling body */
    lv_obj_t *body = lv_obj_create(ov);
    lv_obj_remove_style_all(body);
    lv_obj_set_size(body, UI_HOR_RES, UI_OVERLAY_H - UI_PHOTO_H);
    lv_obj_set_pos(body, 0, UI_PHOTO_H);
    lv_obj_set_style_bg_color(body, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(body, 16, 0);
    lv_obj_set_style_pad_top(body, 10, 0);
    lv_obj_set_style_pad_bottom(body, 20, 0);
    lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(body, LV_DIR_VER);
    lv_obj_set_style_width(body, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(body, ui_c(UI_C_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(body, LV_OPA_COVER, LV_PART_SCROLLBAR);

    lv_obj_t *name = ui_label(body, &adb_font_24, UI_C_TEXT, r->name);
    lv_obj_set_width(name, LV_PCT(100));
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_WRAP);

    char meta[128];
    const char *glass = r->glass != ADB_NONE ? adb_tag_label(ADB_TAG_GLASS, r->glass) : (r->glass_key ? r->glass_key : "");
    char abv[24] = "";
    if (r->abv >= 0) {
        if (r->alcoholic == ADB_ALC_MOCKTAIL || r->abv < 0.5f) snprintf(abv, sizeof abv, "  \xE2\x80\xA2  Alcohol-free");
        else snprintf(abv, sizeof abv, "  \xE2\x80\xA2  ~%.0f%% ABV", (double)r->abv);
    }
    snprintf(meta, sizeof meta, "%s  \xE2\x80\xA2  %s%s", adb_recipe_spirit_label(r),
             r->method != ADB_NONE ? adb_tag_label(ADB_TAG_METHOD, r->method) : "", abv);
    lv_obj_t *m = ui_label(body, &adb_font_14, UI_C_MUTED, meta);
    lv_obj_set_width(m, LV_PCT(100));
    lv_label_set_long_mode(m, LV_LABEL_LONG_MODE_WRAP);

    lv_obj_t *tags = ui_box(body);
    lv_obj_set_size(tags, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(tags, LV_FLEX_FLOW_ROW_WRAP);
    lv_obj_set_style_pad_column(tags, 6, 0);
    lv_obj_set_style_pad_row(tags, 6, 0);
    lv_obj_set_style_pad_top(tags, 8, 0);
    for (int i = 0; i < adb_tag_count(ADB_TAG_FLAVOR); i++)
        if (r->flavor_mask & (1u << i)) ui_small_tag(tags, adb_tag_label(ADB_TAG_FLAVOR, i));
    if (r->difficulty != ADB_NONE) {
        lv_obj_t *t = ui_small_tag(tags, adb_tag_label(ADB_TAG_DIFFICULTY, r->difficulty));
        lv_obj_set_style_text_color(t, ui_c(UI_C_MUTED), 0);
    }

    /* ingredients header with units toggle */
    lv_obj_t *ih = ui_box(body);
    lv_obj_set_size(ih, LV_PCT(100), 56);
    lv_obj_t *it = section_title(ih, "INGREDIENTS");
    lv_obj_align(it, LV_ALIGN_BOTTOM_LEFT, 0, 4);
    static const char *const units[] = { "oz", "ml" };
    lv_obj_t *seg = ui_segmented(ih, units, 2, g_ui.st->settings.use_oz ? 0 : 1, units_cb, NULL);
    lv_obj_set_width(seg, 112);
    lv_obj_align(seg, LV_ALIGN_BOTTOM_RIGHT, 0, 0);

    s_ings_box = ui_box(body);
    lv_obj_set_size(s_ings_box, LV_PCT(100), LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(s_ings_box, LV_FLEX_FLOW_COLUMN);
    fill_ingredients();

    section_title(body, "STEPS");
    for (int i = 0; i < r->n_steps; i++) {
        lv_obj_t *row = ui_box(body);
        lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
        lv_obj_set_style_pad_ver(row, 4, 0);
        lv_obj_t *n = ui_label(row, &adb_font_16, UI_C_ACCENT, "");
        lv_label_set_text_fmt(n, "%d", i + 1);
        lv_obj_t *s = ui_label(row, &adb_font_16, UI_C_TEXT, r->steps[i]);
        lv_obj_set_pos(s, 24, 0);
        lv_obj_set_width(s, UI_HOR_RES - 32 - 24);
        lv_label_set_long_mode(s, LV_LABEL_LONG_MODE_WRAP);
    }

    section_title(body, "GLASS & GARNISH");
    char gg[160];
    const char *ice = r->ice != ADB_NONE && r->ice != 0 ? adb_tag_label(ADB_TAG_ICE, r->ice) : NULL;
    snprintf(gg, sizeof gg, "%s%s%s\n%s", glass, ice ? ", " : "", ice ? ice : "",
             r->garnish ? r->garnish : "No garnish");
    lv_obj_t *g = ui_label(body, &adb_font_16, UI_C_TEXT, gg);
    lv_obj_set_width(g, LV_PCT(100));
    lv_label_set_long_mode(g, LV_LABEL_LONG_MODE_WRAP);

    char credit[256];
    if (r->image.is_photo && r->image.author)
        snprintf(credit, sizeof credit, "Photo: %s, %s%s", r->image.author,
                 r->image.license ? r->image.license : "license unknown", r->image.modified ? " (resized)" : "");
    else
        snprintf(credit, sizeof credit, "Illustration: generated glass icon");
    lv_obj_t *c = ui_label(body, &adb_font_12, UI_C_MUTED, credit);
    lv_obj_set_style_pad_top(c, 16, 0);
    lv_obj_set_width(c, LV_PCT(100));
    lv_label_set_long_mode(c, LV_LABEL_LONG_MODE_WRAP);
}
