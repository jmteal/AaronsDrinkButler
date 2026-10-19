/*
 * ui_platform_t for the board: battery, backlight, UI sounds and idle light sleep.
 *
 * Sounds are short synthesized tones played by a worker task (bsp_audio_play blocks
 * for the clip's length, which must not happen on the LVGL task). Idle sleep watches
 * LVGL's inactivity time and light-sleeps until a touch or BOOT (bsp_sleep_light
 * swallows the waking touch so it doesn't land as a tap). It never sleeps while a USB
 * host is attached: light sleep suspends the USB-Serial/JTAG link, so the console and
 * esptool's auto-reset can't reach the chip until someone touches it. The board has no
 * VBUS sense pin; usb_serial_jtag_is_connected() watches the host's 1 ms SOF packets
 * instead, so on a plain USB charger (no SOF) the device still sleeps.
 */
#include <math.h>
#include <string.h>
#include "app.h"
#include "bsp.h"
#include "driver/usb_serial_jtag.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

static const char *TAG = "platform";

static volatile uint8_t s_brightness = 60;
static volatile bool s_screen_on;            // backlight follows s_brightness once true
static volatile uint16_t s_sleep_s;
static volatile bool s_stay_awake;
static volatile bool s_sound_on = true;
static bool s_audio_ok;
static QueueHandle_t s_sounds;
static char s_version[48];

/* ---- Battery ---- */

static int battery_mv(void *ctx) { return bsp_battery_mv(); }

static int battery_percent(void *ctx)
{
    int mv = bsp_battery_mv();
    return mv < 0 ? -1 : bsp_battery_percent(mv);
}

/* ---- Backlight and sleep timeout ---- */

static void set_brightness(void *ctx, uint8_t pct)
{
    s_brightness = pct;
    if (s_screen_on) bsp_backlight_set(pct);
}

static void set_sleep_timeout(void *ctx, uint16_t s) { s_sleep_s = s; }

void app_platform_screen_on(void)
{
    s_screen_on = true;
    bsp_backlight_set(s_brightness);
}

void app_platform_stay_awake(bool on) { s_stay_awake = on; }

static void idle_task(void *arg)
{
    bool usb_was_attached = false;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(500));
        if (!s_sleep_s || s_stay_awake) continue;
        uint32_t idle_ms = 0;
        if (lvgl_port_lock(100)) {
            idle_ms = lv_display_get_inactive_time(NULL);
            lvgl_port_unlock();
        }
        if (idle_ms < (uint32_t)s_sleep_s * 1000) continue;
        bool usb_attached = usb_serial_jtag_is_connected();
        if (usb_attached != usb_was_attached) {
            ESP_LOGI(TAG, "USB host %s: idle sleep %s", usb_attached ? "attached" : "gone",
                     usb_attached ? "off" : "on");
            usb_was_attached = usb_attached;
        }
        if (usb_attached) continue;
        ESP_LOGI(TAG, "idle %u s: screen off", (unsigned)(idle_ms / 1000));
        bsp_wake_src_t src = bsp_sleep_light(0, NULL);
        ESP_LOGI(TAG, "screen on (woke by %s)", bsp_wake_src_name(src));
        if (lvgl_port_lock(100)) {
            lv_display_trigger_activity(NULL);
            lvgl_port_unlock();
        }
    }
}

void app_platform_idle_start(void)
{
    xTaskCreate(idle_task, "idle_sleep", 3072, NULL, 2, NULL);
}

/* ---- Sounds ---- */

#define SR BSP_AUDIO_SAMPLE_RATE

// Sine burst with a fast attack and exponential decay, added onto out.
static void tone(int16_t *out, int n, float hz, float amp, float decay_ms)
{
    for (int i = 0; i < n; i++) {
        float t = (float)i / SR;
        float env = fminf(1.0f, i / (SR * 0.002f)) * expf(-t * 1000.0f / decay_ms);
        int v = out[i] + (int)(amp * env * sinf(2.0f * (float)M_PI * hz * t));
        out[i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
    }
}

static void sound_task(void *arg)
{
    static int16_t pcm[SR / 4];          // 250 ms max
    for (;;) {
        ui_sound_t s;
        xQueueReceive(s_sounds, &s, portMAX_DELAY);
        int n;
        memset(pcm, 0, sizeof(pcm));
        switch (s) {
        case UI_SOUND_CLINK:            // two glassy partials
            n = SR / 4;
            tone(pcm, n, 2637, 9000, 60);
            tone(pcm, n, 3951, 6000, 45);
            break;
        case UI_SOUND_TOGGLE:
            n = SR * 30 / 1000;
            tone(pcm, n, 1760, 8000, 8);
            break;
        case UI_SOUND_BACK:
            n = SR * 30 / 1000;
            tone(pcm, n, 1175, 8000, 8);
            break;
        case UI_SOUND_TAP:
        default:
            n = SR * 20 / 1000;
            tone(pcm, n, 1568, 7000, 5);
            break;
        }
        bsp_audio_play(pcm, n);
    }
}

static void set_sound(void *ctx, bool on, uint8_t volume)
{
    s_sound_on = on;
    if (s_audio_ok) bsp_audio_set_volume(volume);
}

static void play_sound(void *ctx, ui_sound_t s)
{
    if (s_sound_on && s_sounds) xQueueSend(s_sounds, &s, 0);   // drop it if one is still playing
}

/* ---- Init ---- */

static bool get_time(void *ctx, struct tm *out) { return false; }   // no RTC, no network

void app_platform_init(ui_platform_t *p, const adb_settings_t *settings)
{
    s_brightness = settings->brightness;
    s_sleep_s = settings->sleep_s;
    s_sound_on = settings->sound_on;

    esp_err_t e = bsp_battery_init();
    if (e != ESP_OK) ESP_LOGW(TAG, "battery ADC: %s", esp_err_to_name(e));
    e = bsp_audio_init();
    s_audio_ok = e == ESP_OK;
    if (s_audio_ok) {
        s_sounds = xQueueCreate(1, sizeof(ui_sound_t));
        xTaskCreate(sound_task, "sound", 3072, NULL, 3, NULL);
    } else {
        ESP_LOGW(TAG, "audio: %s (UI sounds off)", esp_err_to_name(e));
    }

    snprintf(s_version, sizeof s_version, "%s", esp_app_get_description()->version);
    memset(p, 0, sizeof(*p));
    p->battery_percent = battery_percent;
    p->battery_mv = battery_mv;
    p->set_brightness = set_brightness;
    p->set_sleep_timeout = set_sleep_timeout;
    p->set_sound = set_sound;
    p->play_sound = play_sound;
    p->get_time = get_time;
    p->version = s_version;
}
