/*
 * Hardware bring-up test for Aaron's Drink Butler (Hosyond ES3C35P).
 *
 * Screen, top to bottom:
 *   - colour bars with their names, and a 16-step grey ramp (checks byte swap, inversion,
 *     colour order)
 *   - live readout: battery mV / %, last touch, touch-INT edge count, free heap
 *   - backlight slider
 *   - Beep / Light sleep / Deep sleep buttons
 *   - touch area with five crosshair targets; every touch leaves a dot (Clear wipes them)
 *
 * Every test logs a line starting with "TEST" to the serial console.
 * Built instead of the app with CONFIG_ADB_HWTEST (sdkconfig.hwtest; see README.md).
 */
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "bsp.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_partition.h"
#include "esp_sleep.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "hwtest";

#define MAX_DOTS        64
#define BEEP_HZ         880
#define BEEP_MS         250
#define BACKLIGHT_START 60      // % set once the UI is built

typedef enum {
    CMD_BEEP,
    CMD_LIGHT_SLEEP,
    CMD_DEEP_SLEEP,
} cmd_t;

static QueueHandle_t s_cmds;
static lv_obj_t *s_info;
static lv_obj_t *s_status;
static lv_obj_t *s_bl_label;
static lv_obj_t *s_dots[MAX_DOTS];
static int s_dot_next;
static volatile uint32_t s_int_edges;
static int s_last_x = -1, s_last_y = -1;
static int s_batt_mv = -1;

/* ---- Hardware checks that need no screen ---- */

static void i2c_scan(void)
{
    i2c_master_bus_handle_t bus = bsp_i2c_bus();
    char found[64] = "";
    int n = 0;
    for (uint16_t addr = 0x08; addr < 0x78; addr++) {
        if (i2c_master_probe(bus, addr, 20) == ESP_OK) {
            n += snprintf(found + n, sizeof(found) - n, " 0x%02X", addr);
        }
    }
    // Expected: 0x18 (ES8311) and 0x55 (touch); 0x28 sometimes appears alongside 0x55
    ESP_LOGI(TAG, "TEST i2c: found%s", n ? found : " nothing");
}

static void report_memory(void)
{
    const esp_partition_t *drinks = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, 0x40, "drinks");
    ESP_LOGI(TAG, "TEST memory: PSRAM %u KB, internal free %u KB, drinks partition %s (%" PRIu32 " KB)",
             (unsigned)(heap_caps_get_total_size(MALLOC_CAP_SPIRAM) / 1024),
             (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
             drinks ? "found" : "MISSING", drinks ? drinks->size / 1024 : 0);
}

static void IRAM_ATTR touch_int_isr(void *arg)
{
    s_int_edges++;
}

// Count falling edges on touch INT so the screen shows whether the controller drives it
static void touch_int_watch(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = BIT64(BSP_TOUCH_INT),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&cfg);
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BSP_TOUCH_INT, touch_int_isr, NULL);
    ESP_LOGI(TAG, "touch INT idle level: %d", gpio_get_level(BSP_TOUCH_INT));
}

/* ---- Audio ---- */

static void play_beep(void)
{
    const int n = BSP_AUDIO_SAMPLE_RATE * BEEP_MS / 1000;
    const int fade = BSP_AUDIO_SAMPLE_RATE * 10 / 1000;     // 10 ms ramps avoid clicks
    int16_t *pcm = heap_caps_malloc(n * sizeof(int16_t), MALLOC_CAP_DEFAULT);
    if (!pcm) {
        ESP_LOGE(TAG, "TEST audio: out of memory");
        return;
    }
    for (int i = 0; i < n; i++) {
        float env = 1.0f;
        if (i < fade) {
            env = (float)i / fade;
        } else if (i > n - fade) {
            env = (float)(n - i) / fade;
        }
        pcm[i] = (int16_t)(12000.0f * env * sinf(2.0f * (float)M_PI * BEEP_HZ * i / BSP_AUDIO_SAMPLE_RATE));
    }
    int64_t t0 = esp_timer_get_time();
    esp_err_t err = bsp_audio_play(pcm, n);
    ESP_LOGI(TAG, "TEST audio: %d Hz %d ms tone -> %s (%lld ms incl. amp ramp)", BEEP_HZ, BEEP_MS,
             esp_err_to_name(err), (esp_timer_get_time() - t0) / 1000);
    free(pcm);
}

/* ---- UI ---- */

static void set_status(const char *text)
{
    if (lvgl_port_lock(0)) {
        lv_label_set_text(s_status, text);
        lvgl_port_unlock();
    }
}

static void slider_cb(lv_event_t *e)
{
    int v = lv_slider_get_value(lv_event_get_target(e));
    bsp_backlight_set(v);
    lv_label_set_text_fmt(s_bl_label, "Backlight %d%%", v);
    if (lv_event_get_code(e) == LV_EVENT_RELEASED) {
        ESP_LOGI(TAG, "TEST backlight: %d%%", v);
    }
}

static void button_cb(lv_event_t *e)
{
    cmd_t cmd = (cmd_t)(intptr_t)lv_event_get_user_data(e);
    xQueueSend(s_cmds, &cmd, 0);
}

static void clear_cb(lv_event_t *e)
{
    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_add_flag(s_dots[i], LV_OBJ_FLAG_HIDDEN);
    }
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int x, int y, int w,
                             lv_event_cb_t cb, void *user)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, 48);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_center(label);
    return btn;
}

// lv_line clips points outside its own box, which starts at (0, 0), so the cross is drawn
// in a 20 x 20 box and that box is centred on (x, y).
static void make_cross(lv_obj_t *parent, int x, int y)
{
    static lv_point_precise_t h[] = {{0, 10}, {20, 10}};
    static lv_point_precise_t v[] = {{10, 0}, {10, 20}};
    lv_point_precise_t *pts[] = {h, v};
    for (int i = 0; i < 2; i++) {
        lv_obj_t *line = lv_line_create(parent);
        lv_line_set_points(line, pts[i], 2);
        lv_obj_set_style_line_color(line, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_line_width(line, 2, 0);
        lv_obj_set_pos(line, x - 10, y - 10);
        lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
    }
}

// Runs in the LVGL task: drop a dot under the finger, on the top layer so it shows over
// every widget.
static void touch_poll_cb(lv_timer_t *t)
{
    lv_indev_t *indev = bsp_display_get_touch();
    if (!indev || lv_indev_get_state(indev) != LV_INDEV_STATE_PRESSED) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    if (p.x == s_last_x && p.y == s_last_y) {
        return;
    }
    if (s_last_x < 0 || abs(p.x - s_last_x) + abs(p.y - s_last_y) > 6) {
        lv_obj_t *dot = s_dots[s_dot_next];
        s_dot_next = (s_dot_next + 1) % MAX_DOTS;
        lv_obj_set_pos(dot, p.x - 4, p.y - 4);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_HIDDEN);
        ESP_LOGI(TAG, "TEST touch: x=%d y=%d", (int)p.x, (int)p.y);
    }
    s_last_x = p.x;
    s_last_y = p.y;
}

static void info_cb(lv_timer_t *t)
{
    static int ticks;
    if (ticks++ % 10 == 0) {
        s_batt_mv = bsp_battery_mv();
    }
    lv_label_set_text_fmt(s_info,
                          "Battery: %d mV  (%d%%)\n"
                          "Last touch: %d, %d\n"
                          "Touch INT edges: %" PRIu32 "  (level %d)\n"
                          "Free: internal %u KB, PSRAM %u KB",
                          s_batt_mv, bsp_battery_percent(s_batt_mv), s_last_x, s_last_y,
                          s_int_edges, gpio_get_level(BSP_TOUCH_INT),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024),
                          (unsigned)(heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024));
}

static void build_ui(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101010), 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "Drink Butler - hardware test");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 4);

    // Colour bars: 8 x 40 px, names underneath
    static const struct {
        uint32_t rgb;
        const char *name;
    } bars[] = {
        {0xFF0000, "RED"}, {0x00FF00, "GRN"}, {0x0000FF, "BLU"}, {0xFFFFFF, "WHT"},
        {0x000000, "BLK"}, {0x00FFFF, "CYN"}, {0xFF00FF, "MAG"}, {0xFFFF00, "YEL"},
    };
    for (int i = 0; i < 8; i++) {
        lv_obj_t *bar = lv_obj_create(scr);
        lv_obj_remove_style_all(bar);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(bars[i].rgb), 0);
        lv_obj_set_pos(bar, i * 40, 32);
        lv_obj_set_size(bar, 40, 56);
        lv_obj_t *name = lv_label_create(scr);
        lv_label_set_text(name, bars[i].name);
        lv_obj_set_style_text_color(name, lv_color_hex(0xC0C0C0), 0);
        lv_obj_set_pos(name, i * 40 + 4, 90);
    }
    // Grey ramp, black to white in 16 steps
    for (int i = 0; i < 16; i++) {
        lv_obj_t *step = lv_obj_create(scr);
        lv_obj_remove_style_all(step);
        lv_obj_set_style_bg_opa(step, LV_OPA_COVER, 0);
        uint8_t g = (uint8_t)(i * 17);
        lv_obj_set_style_bg_color(step, lv_color_make(g, g, g), 0);
        lv_obj_set_pos(step, i * 20, 112);
        lv_obj_set_size(step, 20, 20);
    }

    s_info = lv_label_create(scr);
    lv_obj_set_style_text_color(s_info, lv_color_white(), 0);
    lv_obj_set_pos(s_info, 8, 140);
    lv_label_set_text(s_info, "");

    s_bl_label = lv_label_create(scr);
    lv_obj_set_style_text_color(s_bl_label, lv_color_white(), 0);
    lv_obj_set_pos(s_bl_label, 8, 218);
    lv_obj_t *slider = lv_slider_create(scr);
    lv_obj_set_pos(slider, 20, 246);
    lv_obj_set_size(slider, 280, 16);
    lv_slider_set_range(slider, 5, 100);
    lv_slider_set_value(slider, BACKLIGHT_START, LV_ANIM_OFF);
    lv_obj_set_ext_click_area(slider, 14);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_RELEASED, NULL);
    lv_label_set_text_fmt(s_bl_label, "Backlight %d%%", BACKLIGHT_START);

    make_button(scr, "Beep", 8, 278, 94, button_cb, (void *)CMD_BEEP);
    make_button(scr, "Light sleep", 108, 278, 110, button_cb, (void *)CMD_LIGHT_SLEEP);
    make_button(scr, "Deep sleep", 224, 278, 88, button_cb, (void *)CMD_DEEP_SLEEP);

    s_status = lv_label_create(scr);
    lv_obj_set_style_text_color(s_status, lv_color_hex(0xFFD080), 0);
    lv_obj_set_width(s_status, 304);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_status, 8, 332);
    lv_label_set_text(s_status, "Tap the green crosses; dots should land on them.");

    make_button(scr, "Clear", 232, 410, 80, clear_cb, NULL);

    // Targets on the top layer: four corners and the middle of the touch area
    lv_obj_t *top = lv_layer_top();
    make_cross(top, 12, 12);
    make_cross(top, BSP_LCD_H_RES - 12, 12);
    make_cross(top, 12, BSP_LCD_V_RES - 12);
    make_cross(top, BSP_LCD_H_RES - 12, BSP_LCD_V_RES - 12);
    make_cross(top, 120, 420);

    for (int i = 0; i < MAX_DOTS; i++) {
        lv_obj_t *dot = lv_obj_create(top);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(0xFF4040), 0);
        lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(dot, LV_OBJ_FLAG_HIDDEN);
        s_dots[i] = dot;
    }

    lv_timer_create(touch_poll_cb, 15, NULL);
    lv_timer_create(info_cb, 200, NULL);
    info_cb(NULL);
}

/* ---- Worker: everything that blocks runs here, never in the LVGL task ---- */

static void worker_task(void *arg)
{
    char msg[96];
    for (;;) {
        cmd_t cmd;
        xQueueReceive(s_cmds, &cmd, portMAX_DELAY);
        switch (cmd) {
        case CMD_BEEP:
            set_status("Beep: playing 880 Hz...");
            play_beep();
            set_status("Beep done. Did you hear it?");
            break;
        case CMD_LIGHT_SLEEP: {
            set_status("Light sleep in 1 s. Wake with a touch or BOOT (30 s timeout).");
            vTaskDelay(pdMS_TO_TICKS(1000));
            bool touch_armed = false;
            int64_t t0 = esp_timer_get_time();
            bsp_wake_src_t src = bsp_sleep_light(30000, &touch_armed);
            int ms = (int)((esp_timer_get_time() - t0) / 1000);
            gpio_set_intr_type(BSP_TOUCH_INT, GPIO_INTR_NEGEDGE);   // sleep code cleared it
            gpio_intr_enable(BSP_TOUCH_INT);
            snprintf(msg, sizeof(msg), "Woke by %s after %d ms (touch wake %s).",
                     bsp_wake_src_name(src), ms, touch_armed ? "armed" : "NOT armed");
            ESP_LOGI(TAG, "TEST sleep: %s", msg);
            set_status(msg);
            break;
        }
        case CMD_DEEP_SLEEP:
            set_status("Deep sleep in 1 s. Press BOOT to wake (board restarts).");
            vTaskDelay(pdMS_TO_TICKS(1000));
            ESP_LOGI(TAG, "TEST deep sleep: entering");
            bsp_sleep_deep();
            break;
        }
    }
}

void app_main(void)
{
    esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();
    ESP_LOGI(TAG, "boot, wake cause %d%s", cause,
             cause == ESP_SLEEP_WAKEUP_EXT0 ? " (TEST deep sleep: woke by BOOT)" : "");
    report_memory();

    lv_display_t *disp = bsp_display_start();
    if (!disp) {
        ESP_LOGE(TAG, "TEST display: FAILED to start");
        return;
    }
    ESP_LOGI(TAG, "TEST display: started; touch %s", bsp_display_get_touch() ? "ok" : "MISSING");
    i2c_scan();
    touch_int_watch();

    esp_err_t err = bsp_audio_init();
    ESP_LOGI(TAG, "TEST audio init: %s", esp_err_to_name(err));
    err = bsp_battery_init();
    int mv = bsp_battery_mv();
    ESP_LOGI(TAG, "TEST battery: %s, %d mV, %d%%", esp_err_to_name(err), mv, bsp_battery_percent(mv));
    err = bsp_sd_mount("/sdcard");
    ESP_LOGI(TAG, "TEST sd: %s (optional; no card is fine)", esp_err_to_name(err));

    s_cmds = xQueueCreate(4, sizeof(cmd_t));
    xTaskCreate(worker_task, "hwtest", 4096, NULL, 4, NULL);

    lvgl_port_lock(0);
    build_ui();
    lvgl_port_unlock();
    bsp_backlight_set(BACKLIGHT_START);
    ESP_LOGI(TAG, "TEST ui: built, backlight %d%%", BACKLIGHT_START);
}
