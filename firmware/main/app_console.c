/*
 * Serial debug console (CONFIG_ADB_DEBUG_CONSOLE): line commands on the USB serial
 * port, so the UI can be driven and checked from a computer without touching the
 * board. tools/device/adb_console.py wraps it (including screenshots to PNG).
 *
 *   help                     list commands
 *   stats                    drinks, favorites, memory, image cache
 *   favs                     favorite drink ids
 *   find <text>              drinks whose name contains text
 *   tab home|browse|bar|fav  switch tab
 *   open <drink id>          open a drink's detail screen
 *   close                    close overlays (detail, filter, settings)
 *   tap <x> <y>              press and release at a point (a real LVGL pointer)
 *   drag <x1> <y1> <x2> <y2> [ms]   swipe (scrolls lists)
 *   shot                     screenshot: "SHOT <w> <h> <bytes>", base64 RGB565 lines, "SHOT-END"
 *   stay on|off              keep the screen awake (idle sleep off) until reboot
 *   reboot
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app.h"
#include "bsp.h"
#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#if CONFIG_ADB_DEBUG_CONSOLE

static const char *TAG = "console";

static adb_db_t *s_db;
static adb_state_t *s_st;
static lv_indev_t *s_indev;
static volatile int32_t s_px, s_py;
static volatile bool s_pressed;

static void indev_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    data->point.x = s_px;
    data->point.y = s_py;
    data->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void press(int x, int y, bool down)
{
    s_px = x;
    s_py = y;
    s_pressed = down;
}

static void cmd_drag(int x1, int y1, int x2, int y2, int ms)
{
    int steps = ms / 10 > 1 ? ms / 10 : 1;
    press(x1, y1, true);
    vTaskDelay(pdMS_TO_TICKS(40));
    for (int i = 1; i <= steps; i++) {
        press(x1 + (x2 - x1) * i / steps, y1 + (y2 - y1) * i / steps, true);
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelay(pdMS_TO_TICKS(40));
    press(x2, y2, false);
}

static int find_recipe(const char *id)
{
    int r = adb_db_find_recipe(s_db, id);
    if (r < 0) printf("ERR no drink with id '%s'\n", id);
    return r;
}

static void cmd_stats(void)
{
    int favs = 0;
    for (int i = 0; i < s_db->n_recipes; i++) favs += adb_bit_get(s_st->favorites, (unsigned)i);
    int photos = 0;
    for (int i = 0; i < s_db->n_recipes; i++) photos += s_db->recipes[i].image.is_photo;
    size_t browse = 0;
    if (lvgl_port_lock(1000)) {
        browse = adb_filter_apply(s_db, ui_browse_filter(), NULL, 0);
        lvgl_port_unlock();
    }
    app_images_stats_t im;
    app_images_get_stats(&im);
    printf("STATS drinks=%u photos=%d ingredients=%u browse_matches=%u favorites=%d\n",
           s_db->n_recipes, photos, s_db->n_ingredients, (unsigned)browse, favs);
    printf("STATS thumbs_cached=%u thumb_decodes=%u thumb_hits=%u thumb_max_us=%u photo_decodes=%u "
           "photo_hits=%u photo_max_us=%u failures=%u image_kb=%u\n",
           (unsigned)im.thumbs_cached, (unsigned)im.thumb_decodes, (unsigned)im.thumb_hits,
           (unsigned)im.thumb_us_max, (unsigned)im.photo_decodes, (unsigned)im.photo_hits,
           (unsigned)im.photo_us_max, (unsigned)im.failures, (unsigned)(im.bytes / 1024));
    printf("STATS psram_free_kb=%u internal_free_kb=%u internal_min_kb=%u battery_mv=%d\n",
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024),
           (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
           (unsigned)(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL) / 1024), bsp_battery_mv());
}

static void cmd_favs(void)
{
    printf("FAVS");
    for (int i = 0; i < s_db->n_recipes; i++)
        if (adb_bit_get(s_st->favorites, (unsigned)i)) printf(" %s", s_db->recipes[i].id);
    printf("\n");
}

static void cmd_find(const char *text)
{
    for (int i = 0; i < s_db->n_recipes; i++)
        if (adb_text_contains(s_db->recipes[i].name, text))
            printf("FOUND %s \"%s\"%s\n", s_db->recipes[i].id, s_db->recipes[i].name,
                   s_db->recipes[i].image.is_photo ? " photo" : "");
}

static void put_b64(const uint8_t *p, size_t n)
{
    static const char tbl[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char line[77];
    size_t o = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t v = (uint32_t)p[i] << 16 | (i + 1 < n ? (uint32_t)p[i + 1] << 8 : 0) | (i + 2 < n ? p[i + 2] : 0);
        line[o++] = tbl[(v >> 18) & 63];
        line[o++] = tbl[(v >> 12) & 63];
        line[o++] = i + 1 < n ? tbl[(v >> 6) & 63] : '=';
        line[o++] = i + 2 < n ? tbl[v & 63] : '=';
        if (o == 76 || i + 3 >= n) {
            line[o++] = '\n';
            fwrite(line, 1, o, stdout);
            o = 0;
        }
    }
}

static void cmd_shot(void)
{
    if (!lvgl_port_lock(2000)) {
        printf("ERR LVGL busy\n");
        return;
    }
    lv_draw_buf_t *buf = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
    if (!buf) {
        lvgl_port_unlock();
        printf("ERR snapshot failed (out of memory?)\n");
        return;
    }
    // copy row by row (stride may be padded), then let the UI run while we send
    uint32_t w = buf->header.w, h = buf->header.h;
    uint8_t *px = heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
    if (px)
        for (uint32_t y = 0; y < h; y++) memcpy(px + y * w * 2, buf->data + y * buf->header.stride, w * 2);
    lv_draw_buf_destroy(buf);
    lvgl_port_unlock();
    if (!px) {
        printf("ERR no memory for the screenshot\n");
        return;
    }
    esp_log_level_set("*", ESP_LOG_NONE);   // keep log lines out of the image data
    printf("SHOT %u %u %u\n", (unsigned)w, (unsigned)h, (unsigned)(w * h * 2));
    put_b64(px, w * h * 2);
    printf("SHOT-END\n");
    fflush(stdout);
    esp_log_level_set("*", ESP_LOG_INFO);
    heap_caps_free(px);
}

static bool locked_ui(void (*fn)(void *), void *arg)
{
    if (!lvgl_port_lock(1000)) {
        printf("ERR LVGL busy\n");
        return false;
    }
    fn(arg);
    lvgl_port_unlock();
    return true;
}

static void do_tab(void *arg) { ui_close_overlays(); ui_show_tab((ui_tab_t)(intptr_t)arg); }
static void do_open(void *arg) { ui_open_detail((uint16_t)(intptr_t)arg); }
static void do_close(void *arg) { ui_close_overlays(); }

static void handle(char *line)
{
    char *argv[6] = { 0 };
    int argc = 0;
    for (char *save = NULL, *tok = strtok_r(line, " \t", &save); tok && argc < 6; tok = strtok_r(NULL, " \t", &save))
        argv[argc++] = tok;
    if (!argc) return;
    const char *c = argv[0];
    if (!strcmp(c, "help")) {
        printf("commands: stats favs find tab open close tap drag shot stay reboot\n");
    } else if (!strcmp(c, "stats")) {
        cmd_stats();
    } else if (!strcmp(c, "favs")) {
        cmd_favs();
    } else if (!strcmp(c, "find") && argc > 1) {
        cmd_find(argv[1]);
    } else if (!strcmp(c, "tab") && argc > 1) {
        static const char *names[UI_TAB_COUNT] = { "home", "browse", "bar", "fav" };
        int t = -1;
        for (int i = 0; i < UI_TAB_COUNT; i++)
            if (!strcmp(argv[1], names[i])) t = i;
        if (t < 0) printf("ERR tab home|browse|bar|fav\n");
        else locked_ui(do_tab, (void *)(intptr_t)t);
    } else if (!strcmp(c, "open") && argc > 1) {
        int r = find_recipe(argv[1]);
        if (r >= 0) locked_ui(do_open, (void *)(intptr_t)r);
    } else if (!strcmp(c, "close")) {
        locked_ui(do_close, NULL);
    } else if (!strcmp(c, "tap") && argc > 2) {
        press(atoi(argv[1]), atoi(argv[2]), true);
        vTaskDelay(pdMS_TO_TICKS(120));
        press(atoi(argv[1]), atoi(argv[2]), false);
        vTaskDelay(pdMS_TO_TICKS(150));
    } else if (!strcmp(c, "drag") && argc > 4) {
        cmd_drag(atoi(argv[1]), atoi(argv[2]), atoi(argv[3]), atoi(argv[4]), argc > 5 ? atoi(argv[5]) : 300);
    } else if (!strcmp(c, "shot")) {
        cmd_shot();
        return;   // cmd_shot prints its own terminator
    } else if (!strcmp(c, "stay")) {
        app_platform_stay_awake(argc < 2 || strcmp(argv[1], "off") != 0);
    } else if (!strcmp(c, "reboot")) {
        printf("OK\n");
        fflush(stdout);
        vTaskDelay(pdMS_TO_TICKS(50));
        esp_restart();
    } else {
        printf("ERR unknown command '%s' (try help)\n", c);
        return;
    }
    printf("OK\n");
}

static void console_task(void *arg)
{
    char line[128];
    for (;;) {
        if (!fgets(line, sizeof line, stdin)) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        line[strcspn(line, "\r\n")] = 0;
        handle(line);
        fflush(stdout);
    }
}

void app_console_start(adb_db_t *db, adb_state_t *st)
{
    s_db = db;
    s_st = st;
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.tx_buffer_size = 4096;
    if (usb_serial_jtag_driver_install(&cfg) != ESP_OK) {
        ESP_LOGW(TAG, "USB serial driver install failed; console off");
        return;
    }
    usb_serial_jtag_vfs_use_driver();   // blocking stdin/stdout through the driver
    setvbuf(stdin, NULL, _IONBF, 0);

    lvgl_port_lock(0);
    s_indev = lv_indev_create();
    lv_indev_set_type(s_indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(s_indev, indev_read);
    lvgl_port_unlock();

    // "shot" renders the screen on this task (lv_snapshot_take), which needs a deep stack
    xTaskCreate(console_task, "console", 16 * 1024, NULL, 3, NULL);
    ESP_LOGI(TAG, "debug console ready (type help)");
}

#else
void app_console_start(adb_db_t *db, adb_state_t *st) {}
#endif
