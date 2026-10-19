/*
 * Virtualized (recycled) list with fixed row height.
 *
 * Only enough row objects to cover the viewport (+2) exist. A transparent
 * spacer gives the scroll container its full content height; rows are
 * positioned absolutely at index * row_h and rebound as they scroll into
 * view. Index i always lives in pool slot i % pool_size, so a scroll step
 * only rebinds the row(s) that wrapped around.
 */
#include <stdlib.h>
#include "ui_internal.h"

typedef struct {
    int32_t  row_h;
    uint32_t count;
    uint32_t pool_n;
    lv_obj_t **pool;
    int32_t   *bound;          /* index bound to each pool slot, -1 = none */
    lv_obj_t  *spacer;
    ui_vlist_bind_cb  bind;
    ui_vlist_click_cb click;
    void *user;
} vlist_t;

static vlist_t *get(lv_obj_t *list) { return lv_obj_get_user_data(list); }

static void update(lv_obj_t *list, bool force)
{
    vlist_t *v = get(list);
    int32_t sy = lv_obj_get_scroll_y(list);
    if (sy < 0) sy = 0;
    uint32_t first = (uint32_t)(sy / v->row_h);
    for (uint32_t k = 0; k < v->pool_n; k++) {
        uint32_t idx = first + k;
        lv_obj_t *row = v->pool[idx % v->pool_n];
        int32_t *b = &v->bound[idx % v->pool_n];
        if (idx >= v->count) {
            if (*b != -1 || force) {
                lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
                *b = -1;
            }
            continue;
        }
        if (*b != (int32_t)idx || force) {
            *b = (int32_t)idx;
            lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_y(row, (int32_t)idx * v->row_h);
            v->bind(row, idx, v->user);
        }
    }
}

static void scroll_cb(lv_event_t *e)
{
    update(lv_event_get_current_target_obj(e), false);
}

static void row_click_cb(lv_event_t *e)
{
    lv_obj_t *row = lv_event_get_current_target_obj(e);
    lv_obj_t *list = lv_obj_get_parent(row);
    vlist_t *v = get(list);
    for (uint32_t k = 0; k < v->pool_n; k++)
        if (v->pool[k] == row && v->bound[k] >= 0 && v->click) {
            v->click((uint32_t)v->bound[k], v->user);
            return;
        }
}

static void delete_cb(lv_event_t *e)
{
    vlist_t *v = get(lv_event_get_current_target_obj(e));
    if (!v) return;
    free(v->pool);
    free(v->bound);
    free(v);
}

lv_obj_t *ui_vlist_create(lv_obj_t *parent, int32_t row_h, int32_t max_view_h,
                          ui_vlist_create_cb create, ui_vlist_bind_cb bind,
                          ui_vlist_click_cb click, void *user)
{
    lv_obj_t *list = lv_obj_create(parent);
    lv_obj_remove_style_all(list);
    lv_obj_set_style_bg_color(list, ui_c(UI_C_BG), 0);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(list, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(list, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_width(list, 3, LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_color(list, ui_c(UI_C_MUTED), LV_PART_SCROLLBAR);
    lv_obj_set_style_bg_opa(list, LV_OPA_COVER, LV_PART_SCROLLBAR);
    lv_obj_set_style_pad_right(list, 1, LV_PART_SCROLLBAR);
    lv_obj_remove_flag(list, LV_OBJ_FLAG_SCROLL_ELASTIC);

    vlist_t *v = calloc(1, sizeof(*v));
    v->row_h = row_h;
    v->pool_n = (uint32_t)((max_view_h + row_h - 1) / row_h + 2);
    v->pool = calloc(v->pool_n, sizeof(lv_obj_t *));
    v->bound = malloc(v->pool_n * sizeof(int32_t));
    v->bind = bind;
    v->click = click;
    v->user = user;
    lv_obj_set_user_data(list, v);

    v->spacer = ui_box(list);
    lv_obj_set_size(v->spacer, 1, 0);
    lv_obj_add_flag(v->spacer, LV_OBJ_FLAG_IGNORE_LAYOUT);

    for (uint32_t k = 0; k < v->pool_n; k++) {
        lv_obj_t *row = create(list, user);
        lv_obj_set_size(row, LV_PCT(100), row_h);
        lv_obj_set_pos(row, 0, 0);
        lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_CLICKABLE);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_ON_FOCUS);
        lv_obj_add_flag(row, LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_add_event_cb(row, row_click_cb, LV_EVENT_CLICKED, NULL);
        v->pool[k] = row;
        v->bound[k] = -1;
    }
    lv_obj_add_event_cb(list, scroll_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(list, delete_cb, LV_EVENT_DELETE, NULL);
    return list;
}

void ui_vlist_set_count(lv_obj_t *list, uint32_t count, bool keep_scroll)
{
    vlist_t *v = get(list);
    v->count = count;
    lv_obj_set_height(v->spacer, (int32_t)count * v->row_h);
    lv_obj_update_layout(list);
    if (!keep_scroll) lv_obj_scroll_to_y(list, 0, LV_ANIM_OFF);
    else {
        /* clamp if the list got shorter */
        int32_t max = (int32_t)count * v->row_h - lv_obj_get_content_height(list);
        if (max < 0) max = 0;
        if (lv_obj_get_scroll_y(list) > max) lv_obj_scroll_to_y(list, max, LV_ANIM_OFF);
    }
    update(list, true);
}

void ui_vlist_rebind(lv_obj_t *list)
{
    update(list, true);
}

uint32_t ui_list_pool_size(lv_obj_t *list)
{
    return get(list)->pool_n;
}

uint32_t ui_vlist_count(lv_obj_t *list)
{
    return get(list)->count;
}
