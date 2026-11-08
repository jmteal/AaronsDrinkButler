/*
 * Emulator board (CONFIG_ADB_TARGET_EMULATOR): the whole bsp.h API on Espressif's QEMU
 * for the ESP32-S3. Replaces bsp_display/audio/battery/sd/sleep.c; see firmware/EMULATOR.md.
 *
 *   Display   QEMU's virtual RGB panel (hw/display/esp_rgb.c in espressif/qemu). LVGL
 *             renders straight into the panel's own VRAM at 0x20000000 (direct mode) and
 *             each finished frame is handed to the panel. Nothing waits for QEMU: with no
 *             window open QEMU only copies a frame when tools/emulator asks for a
 *             screendump, so a driver that polled for "update done" would hang.
 *             The register layout matches espressif/esp_lcd_qemu_rgb (v1.0.2), which
 *             isn't used because it would wait, and it would join the real-board build.
 *   Backlight on/off only: 0 shows a black frame, anything else shows the UI.
 *   Touch     none here. The debug console's pointer (down/move/up, sent by the
 *             browser viewer) is the input; it calls bsp_emu_pointer_wake().
 *   Audio     silent, but play/stream block for the clip's real length, so speech
 *             and sound timing behave as on the board.
 *   Battery   a fixed 3950 mV.
 *   Sleep     light sleep pauses LVGL, blanks the screen and waits for a click or the
 *             timeout; deep sleep blanks and restarts on the next click.
 *   SD        not available.
 */
#include <string.h>
#include "bsp.h"

#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "soc/reg_base.h"

static const char *TAG = "bsp_emu";

/* ---- QEMU virtual RGB panel ---- */

// QEMU writes "QEMU" just below SYSCON_DATE_REG; real silicon has something else there.
#define QEMU_ORIGIN_REG     (DR_REG_APB_CTRL_BASE + 0x3F8)
#define QEMU_ORIGIN         0x51454d55

#define RGB_REGS            ((volatile uint32_t *)0x21000000)
#define RGB_VRAM            ((uint8_t *)0x20000000)     // 800 x 600 x 4 bytes
enum {
    RGB_VERSION = 0, RGB_WIN_SIZE, RGB_UPDATE_FROM, RGB_UPDATE_TO,
    RGB_UPDATE_CONTENT, RGB_UPDATE_STATUS, RGB_BPP,
};
#define XY(x, y)            (((uint32_t)(x) << 16) | (uint16_t)(y))

#define FB_BYTES            (BSP_LCD_H_RES * BSP_LCD_V_RES * 2)
#define FB_UI               (RGB_VRAM)                  // LVGL draws here
#define FB_BLACK            (RGB_VRAM + FB_BYTES)       // shown while the "backlight" is off

static lv_display_t *s_disp;
static volatile int s_brightness;

// Point the panel at a whole frame; QEMU copies it on its next refresh or screendump.
static void present(const uint8_t *fb)
{
    RGB_REGS[RGB_UPDATE_FROM] = XY(0, 0);
    RGB_REGS[RGB_UPDATE_TO] = XY(BSP_LCD_H_RES, BSP_LCD_V_RES);
    RGB_REGS[RGB_UPDATE_CONTENT] = (uint32_t)fb;
    RGB_REGS[RGB_UPDATE_STATUS] = 1;
}

static void flush_cb(lv_display_t *disp, const lv_area_t *area, uint8_t *px_map)
{
    // Direct mode: px_map is FB_UI itself, already up to date
    if (lv_display_flush_is_last(disp) && s_brightness > 0) {
        present(FB_UI);
    }
    lv_display_flush_ready(disp);
}

void bsp_backlight_set(int percent)
{
    s_brightness = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    present(s_brightness ? FB_UI : FB_BLACK);
}

int bsp_backlight_get(void)
{
    return s_brightness;
}

lv_display_t *bsp_display_start(void)
{
    if (s_disp) {
        return s_disp;
    }
    if (*(volatile uint32_t *)QEMU_ORIGIN_REG != QEMU_ORIGIN) {
        ESP_LOGE(TAG, "this is the emulator build (CONFIG_ADB_TARGET_EMULATOR); it can't drive a real board");
        return NULL;
    }
    RGB_REGS[RGB_WIN_SIZE] = XY(BSP_LCD_H_RES, BSP_LCD_V_RES);
    RGB_REGS[RGB_BPP] = 16;     // RGB565, host byte order: no swap, unlike the real panel
    memset(FB_UI, 0, FB_BYTES);
    memset(FB_BLACK, 0, FB_BYTES);
    bsp_backlight_set(0);       // like the board: dark until the app turns it on

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16 * 1024;    // the app decodes JPEGs on the LVGL task
    ESP_RETURN_ON_FALSE(lvgl_port_init(&port_cfg) == ESP_OK, NULL, TAG, "lvgl_port_init failed");

    lvgl_port_lock(0);
    s_disp = lv_display_create(BSP_LCD_H_RES, BSP_LCD_V_RES);
    if (s_disp) {
        lv_display_set_color_format(s_disp, LV_COLOR_FORMAT_RGB565);
        lv_display_set_buffers(s_disp, FB_UI, NULL, FB_BYTES, LV_DISPLAY_RENDER_MODE_DIRECT);
        lv_display_set_flush_cb(s_disp, flush_cb);
    }
    lvgl_port_unlock();
    ESP_RETURN_ON_FALSE(s_disp, NULL, TAG, "lv_display_create failed");

    ESP_LOGI(TAG, "QEMU RGB panel %dx%d up (panel v%u.%u), pointer via the serial console",
             BSP_LCD_H_RES, BSP_LCD_V_RES, (unsigned)(RGB_REGS[RGB_VERSION] >> 16),
             (unsigned)(RGB_REGS[RGB_VERSION] & 0xFFFF));
    return s_disp;
}

lv_indev_t *bsp_display_get_touch(void)
{
    return NULL;
}

i2c_master_bus_handle_t bsp_i2c_bus(void)
{
    return NULL;
}

esp_err_t bsp_display_sleep(bool sleep)
{
    present(!sleep && s_brightness ? FB_UI : FB_BLACK);
    return ESP_OK;
}

/* ---- Audio: silent, real-time ---- */

static SemaphoreHandle_t s_audio;   // one player at a time, as on the board
static int s_volume = 60;

static void play_time(size_t samples)
{
    vTaskDelay(pdMS_TO_TICKS((uint64_t)samples * 1000 / BSP_AUDIO_SAMPLE_RATE));
}

esp_err_t bsp_audio_init(void)
{
    if (!s_audio) {
        s_audio = xSemaphoreCreateMutex();
    }
    ESP_LOGI(TAG, "audio: silent in the emulator");
    return s_audio ? ESP_OK : ESP_ERR_NO_MEM;
}

esp_err_t bsp_audio_set_volume(int volume)
{
    s_volume = volume;
    return ESP_OK;
}

esp_err_t bsp_audio_play(const int16_t *pcm, size_t samples)
{
    ESP_RETURN_ON_FALSE(s_audio, ESP_ERR_INVALID_STATE, TAG, "audio not started");
    xSemaphoreTake(s_audio, portMAX_DELAY);
    play_time(samples);
    xSemaphoreGive(s_audio);
    return ESP_OK;
}

esp_err_t bsp_audio_stream_begin(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_audio, ESP_ERR_INVALID_STATE, TAG, "audio not started");
    return xSemaphoreTake(s_audio, pdMS_TO_TICKS(timeout_ms)) ? ESP_OK : ESP_ERR_TIMEOUT;
}

esp_err_t bsp_audio_stream_write(const int16_t *pcm, size_t samples)
{
    play_time(samples);
    return ESP_OK;
}

void bsp_audio_stream_end(void)
{
    xSemaphoreGive(s_audio);
}

/* ---- Battery: fixed ---- */

#define EMU_BATTERY_MV  3950

esp_err_t bsp_battery_init(void)
{
    return ESP_OK;
}

int bsp_battery_mv(void)
{
    return EMU_BATTERY_MV;
}

// Same LiPo curve as bsp_battery.c
int bsp_battery_percent(int mv)
{
    static const struct { int mv, pct; } curve[] = {
        {4200, 100}, {4100, 90}, {4000, 78}, {3900, 67}, {3850, 60}, {3800, 50},
        {3750, 40}, {3700, 32}, {3650, 22}, {3600, 14}, {3500, 7}, {3400, 0},
    };
    const int n = sizeof(curve) / sizeof(curve[0]);
    if (mv >= curve[0].mv) return 100;
    if (mv <= curve[n - 1].mv) return 0;
    for (int i = 1; i < n; i++) {
        if (mv >= curve[i].mv) {
            return curve[i].pct + (mv - curve[i].mv) * (curve[i - 1].pct - curve[i].pct) /
                                  (curve[i - 1].mv - curve[i].mv);
        }
    }
    return 0;
}

/* ---- microSD: none ---- */

esp_err_t bsp_sd_mount(const char *mount_point)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t bsp_sd_unmount(void)
{
    return ESP_ERR_NOT_SUPPORTED;
}

/* ---- Sleep: simulated ---- */

static SemaphoreHandle_t s_wake;
static volatile bool s_asleep;

bool bsp_emu_pointer_wake(void)
{
    if (!s_asleep) {
        return false;
    }
    xSemaphoreGive(s_wake);
    return true;
}

const char *bsp_wake_src_name(bsp_wake_src_t src)
{
    switch (src) {
    case BSP_WAKE_TOUCH: return "touch";
    case BSP_WAKE_BOOT: return "BOOT";
    case BSP_WAKE_TIMER: return "timer";
    case BSP_WAKE_NONE: return "none";
    default: return "other";
    }
}

// Blank and pause LVGL, then wait for a click (or the timeout).
static bool sleep_until_click(uint32_t timeout_ms)
{
    if (!s_wake) {
        s_wake = xSemaphoreCreateBinary();
    }
    xSemaphoreTake(s_wake, 0);
    lvgl_port_stop();
    present(FB_BLACK);
    s_asleep = true;
    bool clicked = xSemaphoreTake(s_wake, timeout_ms ? pdMS_TO_TICKS(timeout_ms) : portMAX_DELAY);
    s_asleep = false;
    return clicked;
}

bsp_wake_src_t bsp_sleep_light(uint32_t timeout_ms, bool *touch_armed)
{
    const int brightness = s_brightness;
    if (touch_armed) {
        *touch_armed = true;
    }
    ESP_LOGI(TAG, "light sleep (emulated; click the screen to wake, timer %lu ms)", (unsigned long)timeout_ms);
    bsp_wake_src_t src = sleep_until_click(timeout_ms) ? BSP_WAKE_TOUCH : BSP_WAKE_TIMER;
    ESP_LOGI(TAG, "woke: %s", bsp_wake_src_name(src));
    lvgl_port_resume();
    if (lvgl_port_lock(0)) {
        lv_obj_invalidate(lv_screen_active());
        lvgl_port_unlock();
    }
    bsp_backlight_set(brightness);
    return src;
}

void bsp_sleep_deep(void)
{
    ESP_LOGI(TAG, "deep sleep (emulated; click the screen to restart, as BOOT would)");
    sleep_until_click(0);
    esp_restart();
}
