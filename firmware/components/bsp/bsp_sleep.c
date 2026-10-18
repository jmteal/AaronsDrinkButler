/*
 * Light sleep with touch-INT or BOOT wake, and deep sleep with BOOT wake.
 *
 * GPIO47 (touch INT) is not an RTC GPIO, so it can wake the chip from light sleep only.
 * Whether the touch controller pulls INT low while the panel is in SLPIN is not yet
 * verified on this board; bsp_sleep_light() reports what woke it so the test app can
 * check. GPIO0 (BOOT) is an RTC GPIO and wakes from both.
 */
#include "bsp.h"
#include "bsp_priv.h"

#include "driver/rtc_io.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define RELEASE_WAIT_MS 2000

static const char *TAG = "bsp_sleep";

const char *bsp_wake_src_name(bsp_wake_src_t src)
{
    switch (src) {
    case BSP_WAKE_TOUCH: return "touch";
    case BSP_WAKE_BOOT:  return "BOOT button";
    case BSP_WAKE_TIMER: return "timer";
    case BSP_WAKE_OTHER: return "other";
    default:             return "none";
    }
}

// Input with pull-up. Any interrupt the app has on the pin is masked: wake arming turns
// the pin into a low-level interrupt, which would fire nonstop while a finger is down.
// gpio_wakeup_disable() also clears the interrupt type, so apps re-arm their own pin
// interrupts after waking (gpio_set_intr_type + gpio_intr_enable).
static void input_pin(gpio_num_t pin)
{
    gpio_intr_disable(pin);
    gpio_set_direction(pin, GPIO_MODE_INPUT);
    gpio_set_pull_mode(pin, GPIO_PULLUP_ONLY);
}

// Wait until `pin` reads high (released), up to `ms`. True if it did.
static bool wait_high(gpio_num_t pin, int ms)
{
    for (int t = 0; t < ms; t += 10) {
        if (gpio_get_level(pin)) {
            return true;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    return gpio_get_level(pin);
}

static void screen_off(void)
{
    bsp_backlight_set(0);
    if (bsp_priv_panel()) {
        bsp_display_sleep(true);
    }
}

bsp_wake_src_t bsp_sleep_light(uint32_t timeout_ms, bool *touch_armed)
{
    const int brightness = bsp_backlight_get();
    input_pin(BSP_TOUCH_INT);
    input_pin(BSP_BOOT_BUTTON);

    // Let go of the finger that pressed "sleep" before arming touch wake
    bool touch_ok = wait_high(BSP_TOUCH_INT, RELEASE_WAIT_MS);
    wait_high(BSP_BOOT_BUTTON, RELEASE_WAIT_MS);
    if (!touch_ok) {
        ESP_LOGW(TAG, "touch INT still low after %d ms; sleeping without touch wake", RELEASE_WAIT_MS);
    }
    if (touch_armed) {
        *touch_armed = touch_ok;
    }

    lvgl_port_stop();
    screen_off();
    gpio_set_level(BSP_AMP_ENABLE, 1);
    gpio_hold_en(BSP_AMP_ENABLE);

    if (touch_ok) {
        gpio_wakeup_enable(BSP_TOUCH_INT, GPIO_INTR_LOW_LEVEL);
    }
    gpio_wakeup_enable(BSP_BOOT_BUTTON, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    if (timeout_ms) {
        esp_sleep_enable_timer_wakeup((uint64_t)timeout_ms * 1000);
    }

    ESP_LOGI(TAG, "light sleep (touch wake %s, timer %lu ms)", touch_ok ? "on" : "off",
             (unsigned long)timeout_ms);
    vTaskDelay(pdMS_TO_TICKS(20));      // let the log drain
    esp_light_sleep_start();

    // Read the pins straight away, before the finger lifts
    const int boot_level = gpio_get_level(BSP_BOOT_BUTTON);
    const int int_level = gpio_get_level(BSP_TOUCH_INT);
    const esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    gpio_wakeup_disable(BSP_TOUCH_INT);
    gpio_wakeup_disable(BSP_BOOT_BUTTON);
    gpio_hold_dis(BSP_AMP_ENABLE);

    bsp_wake_src_t src = BSP_WAKE_OTHER;
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        src = BSP_WAKE_TIMER;
    } else if (cause == ESP_SLEEP_WAKEUP_GPIO) {
        src = boot_level == 0 ? BSP_WAKE_BOOT : int_level == 0 ? BSP_WAKE_TOUCH : BSP_WAKE_OTHER;
    }
    ESP_LOGI(TAG, "woke: cause %d -> %s (BOOT=%d INT=%d)", cause, bsp_wake_src_name(src),
             boot_level, int_level);

    if (bsp_priv_panel()) {
        bsp_display_sleep(false);
    }
    // Don't let the waking touch land as a tap on whatever is under the finger
    wait_high(BSP_TOUCH_INT, RELEASE_WAIT_MS);
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
    input_pin(BSP_BOOT_BUTTON);
    wait_high(BSP_BOOT_BUTTON, RELEASE_WAIT_MS);

    lvgl_port_stop();
    screen_off();
    // Keep the amp off: hold its pin high through deep sleep (the pull-up does too)
    gpio_set_level(BSP_AMP_ENABLE, 1);
    gpio_hold_en(BSP_AMP_ENABLE);
    gpio_deep_sleep_hold_en();

    rtc_gpio_pullup_en(BSP_BOOT_BUTTON);
    rtc_gpio_pulldown_dis(BSP_BOOT_BUTTON);
    esp_sleep_enable_ext0_wakeup(BSP_BOOT_BUTTON, 0);

    ESP_LOGI(TAG, "deep sleep; press BOOT to wake");
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_deep_sleep_start();
}
