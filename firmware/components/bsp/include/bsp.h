#pragma once

/*
 * Board support for Aaron's Drink Butler on the Hosyond ES3C35P.
 *
 * Call bsp_display_start() first: it brings up the shared I2C bus, the panel, LVGL and
 * touch. The other parts (audio, battery, SD) can be started in any order after it.
 * All LVGL calls from other tasks must be wrapped in lvgl_port_lock()/lvgl_port_unlock().
 */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_err.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"

#include "bsp_pins.h"

#ifdef __cplusplus
extern "C" {
#endif

// Native panel geometry (portrait). Touch reports in this frame too.
#define BSP_LCD_H_RES   320
#define BSP_LCD_V_RES   480

/* ---- Display, touch, backlight ---- */

/**
 * Initialise I2C, the ST77922 panel (QSPI 40 MHz, byte-swapped RGB565, inversion on),
 * the LVGL port task and the touch controller. Draws into two partial buffers in
 * internal DMA RAM; areas are rounded to 4-pixel boundaries as the panel requires.
 *
 * The backlight stays off until bsp_backlight_set(), so power-on garbage is never shown.
 * Returns NULL if the panel could not be started. If only touch fails, the display is
 * still returned and bsp_display_get_touch() returns NULL.
 */
lv_display_t *bsp_display_start(void);

lv_indev_t *bsp_display_get_touch(void);

// Shared I2C master bus (touch 0x55, codec 0x18). Valid after bsp_display_start().
i2c_master_bus_handle_t bsp_i2c_bus(void);

// Put the panel into SLPIN (true) or bring it back with SLPOUT (false).
esp_err_t bsp_display_sleep(bool sleep);

// 0 (off) to 100, on a perceptual (squared) curve, LEDC PWM at 25 kHz. Any task.
void bsp_backlight_set(int percent);
int bsp_backlight_get(void);

/* ---- Audio: ES8311 codec + SC8002B amp ---- */

#define BSP_AUDIO_SAMPLE_RATE   16000

// Start I2S and the codec, 16-bit mono at BSP_AUDIO_SAMPLE_RATE. The amp stays off.
esp_err_t bsp_audio_init(void);

// 0-100. Default 60.
esp_err_t bsp_audio_set_volume(int volume);

/**
 * Play 16-bit mono PCM at BSP_AUDIO_SAMPLE_RATE. Blocks until the samples have been
 * played. Switches the amp on just before and off afterwards. Safe from any task
 * (not from the LVGL task: it would freeze the UI for the clip's length).
 */
esp_err_t bsp_audio_play(const int16_t *pcm, size_t samples);

/* ---- Battery ---- */

esp_err_t bsp_battery_init(void);

// Battery voltage in mV (ADC on GPIO8 x 2, averaged). Negative on error.
int bsp_battery_mv(void);

// LiPo state of charge 0-100 for a voltage from bsp_battery_mv() (rough, load-dependent).
int bsp_battery_percent(int mv);

/* ---- microSD (optional) ---- */

// Mount a FAT card at `mount_point` (e.g. "/sdcard"). Never formats.
esp_err_t bsp_sd_mount(const char *mount_point);
esp_err_t bsp_sd_unmount(void);

/* ---- Sleep ---- */

typedef enum {
    BSP_WAKE_NONE = 0,
    BSP_WAKE_TOUCH,     // touch INT (GPIO47) went low
    BSP_WAKE_BOOT,      // BOOT button (GPIO0)
    BSP_WAKE_TIMER,
    BSP_WAKE_OTHER,
} bsp_wake_src_t;

/**
 * Backlight off, panel SLPIN, LVGL paused, then light sleep until touch, BOOT or
 * `timeout_ms` (0 = no timer). On return the panel, LVGL and backlight are restored.
 * Call from a task other than the LVGL task, without holding the LVGL lock.
 *
 * If touch INT is already low before sleeping (finger still down, or the controller
 * holds INT low), it waits up to 2 s for release, then sleeps without touch wake.
 * `touch_armed` (may be NULL) reports whether touch wake was enabled.
 *
 * Interrupts the app set on GPIO47 or GPIO0 are disabled and their type cleared;
 * re-arm them after this returns.
 */
bsp_wake_src_t bsp_sleep_light(uint32_t timeout_ms, bool *touch_armed);

// Backlight off, panel SLPIN, amp held off, deep sleep until BOOT is pressed. Does not
// return: the chip restarts on wake (the panel does not, so init sends SWRESET).
void bsp_sleep_deep(void);

const char *bsp_wake_src_name(bsp_wake_src_t src);

#ifdef __cplusplus
}
#endif
