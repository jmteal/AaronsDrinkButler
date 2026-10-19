/* Dark "bar" theme and small widget helpers. Everything is flat and opaque:
 * no shadows, gradients or transparency, so scrolling stays cheap on the
 * ESP32-S3's partial-buffer renderer. */
#include <stdint.h>
#include "ui_internal.h"

static lv_style_t s_btn, s_btn_primary, s_btn_pressed, s_chip, s_chip_checked, s_tag, s_icon_btn;
static bool s_inited;

void ui_theme_init(void)
{
    lv_display_t *disp = lv_display_get_default();
    lv_theme_t *th = lv_theme_default_init(disp, ui_c(UI_C_ACCENT), ui_c(UI_C_ACCENT2), true, &adb_font_16);
    lv_display_set_theme(disp, th);

    if (s_inited) return;
    s_inited = true;

    lv_style_init(&s_btn);
    lv_style_set_bg_color(&s_btn, ui_c(UI_C_SURFACE2));
    lv_style_set_bg_opa(&s_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_btn, 12);
    lv_style_set_shadow_width(&s_btn, 0);
    lv_style_set_border_width(&s_btn, 0);
    lv_style_set_text_color(&s_btn, ui_c(UI_C_TEXT));
    lv_style_set_text_font(&s_btn, &adb_font_16);
    lv_style_set_pad_hor(&s_btn, 12);
    lv_style_set_pad_ver(&s_btn, 6);
    lv_style_set_min_height(&s_btn, UI_TOUCH);

    lv_style_init(&s_btn_primary);
    lv_style_set_bg_color(&s_btn_primary, ui_c(UI_C_ACCENT));
    lv_style_set_text_color(&s_btn_primary, ui_c(UI_C_ON_ACCENT));

    lv_style_init(&s_btn_pressed);
    lv_style_set_bg_color(&s_btn_pressed, ui_c(UI_C_DIVIDER));
    lv_style_set_transform_width(&s_btn_pressed, 0);
    lv_style_set_transform_height(&s_btn_pressed, 0);

    lv_style_init(&s_chip);
    lv_style_set_bg_color(&s_chip, ui_c(UI_C_SURFACE2));
    lv_style_set_bg_opa(&s_chip, LV_OPA_COVER);
    lv_style_set_radius(&s_chip, LV_RADIUS_CIRCLE);
    lv_style_set_border_width(&s_chip, 1);
    lv_style_set_border_color(&s_chip, ui_c(UI_C_DIVIDER));
    lv_style_set_shadow_width(&s_chip, 0);
    lv_style_set_text_color(&s_chip, ui_c(UI_C_TEXT));
    lv_style_set_text_font(&s_chip, &adb_font_14);
    lv_style_set_pad_hor(&s_chip, 14);
    lv_style_set_pad_ver(&s_chip, 0);
    lv_style_set_height(&s_chip, UI_TOUCH);

    lv_style_init(&s_chip_checked);
    lv_style_set_bg_color(&s_chip_checked, ui_c(UI_C_ACCENT));
    lv_style_set_border_color(&s_chip_checked, ui_c(UI_C_ACCENT));
    lv_style_set_text_color(&s_chip_checked, ui_c(UI_C_ON_ACCENT));

    lv_style_init(&s_tag);
    lv_style_set_bg_color(&s_tag, ui_c(UI_C_SURFACE2));
    lv_style_set_bg_opa(&s_tag, LV_OPA_COVER);
    lv_style_set_radius(&s_tag, 9);
    lv_style_set_pad_hor(&s_tag, 8);
    lv_style_set_pad_ver(&s_tag, 2);
    lv_style_set_text_color(&s_tag, ui_c(UI_C_ACCENT));
    lv_style_set_text_font(&s_tag, &adb_font_12);

    lv_style_init(&s_icon_btn);
    lv_style_set_bg_color(&s_icon_btn, ui_c(UI_C_SURFACE2));
    lv_style_set_bg_opa(&s_icon_btn, LV_OPA_COVER);
    lv_style_set_radius(&s_icon_btn, LV_RADIUS_CIRCLE);
    lv_style_set_shadow_width(&s_icon_btn, 0);
    lv_style_set_border_width(&s_icon_btn, 0);
    lv_style_set_pad_all(&s_icon_btn, 0);
    lv_style_set_text_color(&s_icon_btn, ui_c(UI_C_TEXT));
    lv_style_set_text_font(&s_icon_btn, &adb_font_20);
}

lv_obj_t *ui_box(lv_obj_t *parent)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
    return o;
}

lv_obj_t *ui_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, ui_c(color), 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *ui_button(lv_obj_t *parent, const char *text, bool primary)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &s_btn, 0);
    if (primary) lv_obj_add_style(b, &s_btn_primary, 0);
    lv_obj_add_style(b, &s_btn_pressed, LV_STATE_PRESSED);
    lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (text) {
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, text);
        lv_obj_center(l);
    }
    return b;
}

lv_obj_t *ui_icon_button(lv_obj_t *parent, const char *symbol, int size)
{
    lv_obj_t *b = lv_button_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_add_style(b, &s_icon_btn, 0);
    lv_obj_add_style(b, &s_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_size(b, size, size);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, symbol);
    lv_obj_center(l);
    return b;
}

lv_obj_t *ui_chip(lv_obj_t *parent, const char *text, bool checkable)
{
    lv_obj_t *c = lv_button_create(parent);
    lv_obj_remove_style_all(c);
    lv_obj_add_style(c, &s_chip, 0);
    lv_obj_add_style(c, &s_chip_checked, LV_STATE_CHECKED);
    lv_obj_add_style(c, &s_btn_pressed, LV_STATE_PRESSED);
    lv_obj_set_width(c, LV_SIZE_CONTENT);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLL_ON_FOCUS);
    if (checkable) lv_obj_add_flag(c, LV_OBJ_FLAG_CHECKABLE);
    lv_obj_t *l = lv_label_create(c);
    lv_label_set_text(l, text);
    lv_obj_center(l);
    return c;
}

void ui_chip_set_checked(lv_obj_t *chip, bool on)
{
    if (on) lv_obj_add_state(chip, LV_STATE_CHECKED);
    else    lv_obj_remove_state(chip, LV_STATE_CHECKED);
}

lv_obj_t *ui_small_tag(lv_obj_t *parent, const char *text)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_add_style(l, &s_tag, 0);
    lv_label_set_text(l, text);
    return l;
}

lv_obj_t *ui_header(lv_obj_t *parent, const char *title, lv_event_cb_t back_cb)
{
    lv_obj_t *h = ui_box(parent);
    lv_obj_set_size(h, LV_PCT(100), 52);
    lv_obj_set_style_bg_color(h, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(h, LV_OPA_COVER, 0);
    lv_obj_set_style_border_side(h, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(h, 1, 0);
    lv_obj_set_style_border_color(h, ui_c(UI_C_DIVIDER), 0);
    int x = 12;
    if (back_cb) {
        lv_obj_t *b = ui_icon_button(h, LV_SYMBOL_LEFT, UI_TOUCH);
        lv_obj_set_style_bg_opa(b, LV_OPA_TRANSP, 0);
        lv_obj_align(b, LV_ALIGN_LEFT_MID, 4, 0);
        lv_obj_add_event_cb(b, back_cb, LV_EVENT_CLICKED, NULL);
        x = 52;
    }
    lv_obj_t *t = ui_label(h, &adb_font_20, UI_C_TEXT, title);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, x, 0);
    return h;
}

/* ---- Segmented control ------------------------------------------------ */

static void seg_btn_cb(lv_event_t *e)
{
    lv_obj_t *btn = lv_event_get_target_obj(e);
    lv_obj_t *seg = lv_obj_get_parent(btn);
    int idx = (int)lv_obj_get_index(btn);
    if ((intptr_t)lv_obj_get_user_data(seg) == idx) return;
    ui_segmented_select(seg, idx);
    ui_sound(UI_SOUND_TOGGLE);
    lv_obj_send_event(seg, LV_EVENT_VALUE_CHANGED, NULL);
}

lv_obj_t *ui_segmented(lv_obj_t *parent, const char *const *labels, int n, int selected,
                       lv_event_cb_t cb, void *user)
{
    lv_obj_t *seg = ui_box(parent);
    lv_obj_set_height(seg, UI_TOUCH);
    lv_obj_set_style_bg_color(seg, ui_c(UI_C_SURFACE2), 0);
    lv_obj_set_style_bg_opa(seg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(seg, 12, 0);
    lv_obj_set_style_pad_all(seg, 3, 0);
    lv_obj_set_style_pad_column(seg, 3, 0);
    lv_obj_set_flex_flow(seg, LV_FLEX_FLOW_ROW);
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = lv_button_create(seg);
        lv_obj_remove_style_all(b);
        lv_obj_set_height(b, LV_PCT(100));
        lv_obj_set_flex_grow(b, 1);
        lv_obj_set_style_radius(b, 9, 0);
        lv_obj_set_style_bg_color(b, ui_c(UI_C_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(b, ui_c(UI_C_TEXT), 0);
        lv_obj_set_style_text_color(b, ui_c(UI_C_ON_ACCENT), LV_STATE_CHECKED);
        lv_obj_set_style_text_font(b, &adb_font_14, 0);
        lv_obj_set_ext_click_area(b, 3);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, labels[i]);
        lv_obj_center(l);
        lv_obj_add_event_cb(b, seg_btn_cb, LV_EVENT_CLICKED, NULL);
    }
    ui_segmented_select(seg, selected);
    if (cb) lv_obj_add_event_cb(seg, cb, LV_EVENT_VALUE_CHANGED, user);
    return seg;
}

void ui_segmented_select(lv_obj_t *seg, int selected)
{
    lv_obj_set_user_data(seg, (void *)(intptr_t)selected);
    uint32_t n = lv_obj_get_child_count(seg);
    for (uint32_t i = 0; i < n; i++) ui_chip_set_checked(lv_obj_get_child(seg, (int32_t)i), (int)i == selected);
}
