/*
 * ST77922 QSPI panel, backlight and touch for the ES3C35P.
 *
 * Board specifics (docs/research/board_es3c35p.md):
 *   - LCD reset is tied to CHIP_PU, so there is no reset GPIO and the driver sends
 *     SWRESET. A USB/software reset does not reset the panel: after flashing over other
 *     firmware, unplug the board once if the screen stays black.
 *   - RGB element order, inversion on, and RGB565 byte swap (the panel is big-endian).
 *   - Draw windows must start and end on 4-pixel boundaries.
 *   - QSPI at 40 MHz (controller write limit 62.5 MHz; the SPI clock divides from 80).
 */
#include "bsp.h"
#include "bsp_priv.h"

#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_st77922.h"
#include "esp_lcd_touch_st7123.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define LCD_HOST        SPI2_HOST
#define LCD_PIXEL_HZ    (40 * 1000 * 1000)
#define LCD_BUF_LINES   48      // per buffer: 320 x 48 x 2 B = 30 KB, two of them

#define BL_MODE         LEDC_LOW_SPEED_MODE
#define BL_TIMER        LEDC_TIMER_0
#define BL_CHANNEL      LEDC_CHANNEL_0
#define BL_PWM_HZ       25000   // above hearing range
#define BL_DUTY_BITS    LEDC_TIMER_10_BIT
#define BL_DUTY_MAX     ((1 << 10) - 1)

#define I2C_PORT        I2C_NUM_0

static const char *TAG = "bsp_disp";

static i2c_master_bus_handle_t s_i2c;
static esp_lcd_panel_io_handle_t s_io;
static esp_lcd_panel_handle_t s_panel;
static lv_display_t *s_disp;
static lv_indev_t *s_touch;
static int s_brightness;

/*
 * Vendor init sequence for this panel variant, as run by the factory firmware.
 * Register values decoded by github.com/jlmeredith/ES3C35P and used by
 * github.com/andrewscherpbier/esp32-s3-display.
 */
static const st77922_lcd_init_cmd_t lcd_init_cmds[] = {
    {0xF1, (uint8_t []){0x00}, 1, 0},
    {0x60, (uint8_t []){0x00, 0x00, 0x00}, 3, 0},
    {0x65, (uint8_t []){0x80}, 1, 0},
    {0x79, (uint8_t []){0x06}, 1, 0},
    {0x7B, (uint8_t []){0x00, 0x08, 0x08}, 3, 0},
    {0x80, (uint8_t []){0x55, 0x62, 0x2F, 0x17, 0xF0, 0x52, 0x70, 0xD2, 0x52, 0x62, 0xEA}, 11, 0},
    {0x81, (uint8_t []){0x26, 0x52, 0x72, 0x27}, 4, 0},
    {0x84, (uint8_t []){0x92, 0x25}, 2, 0},
    {0x87, (uint8_t []){0x10, 0x10, 0x58, 0x00, 0x02, 0x3A}, 6, 0},
    {0x88, (uint8_t []){0x00, 0x00, 0x2C, 0x10, 0x04, 0x00, 0x00, 0x00, 0x01, 0x01, 0x01, 0x01, 0x01, 0x00, 0x06}, 15, 0},
    {0x89, (uint8_t []){0x00, 0x00, 0x00}, 3, 0},
    {0x8A, (uint8_t []){0x13, 0x00, 0x2C, 0x00, 0x00, 0x2C, 0x10, 0x10, 0x00, 0x3E, 0x19}, 11, 0},
    {0x8B, (uint8_t []){0x15, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x97, 0x8E}, 9, 0},
    {0x8C, (uint8_t []){0x1D, 0xB1, 0xB1, 0x44, 0x96, 0x2C, 0x10, 0x50, 0x0F, 0x01, 0xC5, 0x12, 0x09}, 13, 0},
    {0x8D, (uint8_t []){0x0C}, 1, 0},
    {0x8E, (uint8_t []){0x33, 0x01, 0x0C, 0x13, 0x01, 0x01}, 6, 0},
    {0xB3, (uint8_t []){0x00, 0x30}, 2, 0},
    {0xF1, (uint8_t []){0x00}, 1, 0},
    {0x71, (uint8_t []){0xD0}, 1, 0},
    {0x66, (uint8_t []){0x02, 0x3F}, 2, 0},
    {0xBE, (uint8_t []){0x26, 0x00, 0x9D}, 3, 0},
    {0x70, (uint8_t []){0x01, 0xA6, 0x11, 0x40, 0xE0, 0x00, 0x11, 0x60, 0x11, 0x00, 0x00, 0x1A}, 12, 0},
    {0x90, (uint8_t []){0x04, 0x04, 0x55, 0x74, 0x00, 0x40, 0x43, 0x2D, 0x2D}, 9, 0},
    {0x91, (uint8_t []){0x04, 0x04, 0x55, 0x75, 0x00, 0x40, 0x42, 0x2D, 0x2D}, 9, 0},
    {0x92, (uint8_t []){0x04, 0x44, 0x55, 0xC0, 0x06, 0x00, 0x07, 0x05, 0x90, 0x2D}, 10, 0},
    {0x93, (uint8_t []){0x04, 0x43, 0x11, 0x00, 0x00, 0x00, 0x00, 0x05, 0x90, 0x2D}, 10, 0},
    {0x94, (uint8_t []){0x00, 0x00, 0x00, 0x00, 0x00, 0x00}, 6, 0},
    {0x95, (uint8_t []){0x96, 0x16, 0x00, 0x00, 0xFF}, 5, 0},
    {0x96, (uint8_t []){0x44, 0x53, 0x03, 0x12, 0x23, 0x24, 0x06, 0x05, 0x9A, 0x2D, 0x00, 0x44}, 12, 0},
    {0x97, (uint8_t []){0x44, 0x53, 0x47, 0x56, 0x20, 0x20, 0x02, 0x01, 0x9A, 0x2D, 0x00, 0x44}, 12, 0},
    {0xBA, (uint8_t []){0x55, 0x9A, 0x2D, 0x9A, 0x2D}, 5, 0},
    {0x9A, (uint8_t []){0x40, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9B, (uint8_t []){0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00}, 7, 0},
    {0x9C, (uint8_t []){0x5C, 0x12, 0x00, 0x00, 0x10, 0x12, 0x00, 0x00, 0x10, 0x02, 0x00, 0x00, 0x00}, 13, 0},
    {0x9D, (uint8_t []){0x8A, 0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 8, 0},
    {0x9E, (uint8_t []){0x51, 0x00, 0x00, 0x00, 0x80, 0x1E, 0x01}, 7, 0},
    {0xB4, (uint8_t []){0x1D, 0x1C, 0x1E, 0x0B, 0x14, 0x02, 0x13, 0x09, 0x1E, 0x00, 0x1E, 0x10}, 12, 0},
    {0xB5, (uint8_t []){0x1D, 0x1C, 0x1E, 0x0A, 0x15, 0x03, 0x11, 0x08, 0x1E, 0x01, 0x1E, 0x12}, 12, 0},
    {0xB6, (uint8_t []){0x77, 0x77, 0x00, 0x0A, 0xFF, 0x0A, 0xFF}, 7, 0},
    {0x86, (uint8_t []){0xC6, 0x04, 0xB1, 0x02, 0x58, 0x12, 0x58, 0x0C, 0x13, 0x01, 0xA5, 0x00, 0xA5, 0xA5}, 14, 0},
    {0xB7, (uint8_t []){0x07, 0x0A, 0x0E, 0x06, 0x05, 0x03, 0x2B, 0x03, 0x03, 0x42, 0x07, 0x10, 0x10, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB8, (uint8_t []){0x07, 0x0A, 0x0D, 0x05, 0x05, 0x02, 0x2B, 0x02, 0x03, 0x42, 0x06, 0x10, 0x0F, 0x2E, 0x3F, 0x0D}, 16, 0},
    {0xB9, (uint8_t []){0x23, 0x23}, 2, 0},
    {0xBF, (uint8_t []){0x10, 0x14, 0x14, 0x0B, 0x0B, 0x0B}, 6, 0},
    {0xF2, (uint8_t []){0x00}, 1, 0},
    {0x73, (uint8_t []){0x04, 0xDA, 0x12, 0x54, 0x47}, 5, 0},
    {0x77, (uint8_t []){0x6B, 0x5B, 0xFD, 0xC3, 0xC5}, 5, 0},
    {0x7A, (uint8_t []){0x15, 0x27}, 2, 0},
    {0x7B, (uint8_t []){0x04, 0x57}, 2, 0},
    {0x7E, (uint8_t []){0x01, 0x0E}, 2, 0},
    {0xBF, (uint8_t []){0x36}, 1, 0},
    {0xE3, (uint8_t []){0x40, 0x40}, 2, 0},
    {0xF0, (uint8_t []){0x00}, 1, 0},
    {0xD0, (uint8_t []){0x00}, 1, 0},
    {0x2A, (uint8_t []){0x00, 0x00, 0x01, 0x3F}, 4, 0},
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0xDF}, 4, 0},
    {0x21, NULL, 0, 0},                         // INVON
    {0x11, NULL, 0, 120},                       // SLPOUT
    {0x29, NULL, 0, 0},                         // DISPON
    {0x3A, (uint8_t []){0x01}, 1, 0},           // COLMOD: RGB565
    {0x36, (uint8_t []){0x00}, 1, 0},           // MADCTL: RGB order, no mirroring
    {0x35, (uint8_t []){0x01}, 1, 20},          // TEON
};

esp_lcd_panel_handle_t bsp_priv_panel(void)
{
    return s_panel;
}

i2c_master_bus_handle_t bsp_i2c_bus(void)
{
    return s_i2c;
}

lv_indev_t *bsp_display_get_touch(void)
{
    return s_touch;
}

// The touch controller can hold SDA low while in reset, so release it before the bus
// starts. The ST7123 driver's own reset waits only 10 ms; this board needs longer.
static void touch_reset(void)
{
    const gpio_config_t cfg = {
        .pin_bit_mask = BIT64(BSP_TOUCH_RST),
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&cfg);
    gpio_set_level(BSP_TOUCH_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BSP_TOUCH_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(100));
}

static esp_err_t i2c_start(void)
{
    touch_reset();
    const i2c_master_bus_config_t cfg = {
        .i2c_port = I2C_PORT,
        .sda_io_num = BSP_I2C_SDA,
        .scl_io_num = BSP_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,   // board has 10k externals as well
    };
    return i2c_new_master_bus(&cfg, &s_i2c);
}

static esp_err_t backlight_start(void)
{
    const ledc_timer_config_t timer = {
        .speed_mode = BL_MODE,
        .duty_resolution = BL_DUTY_BITS,
        .timer_num = BL_TIMER,
        .freq_hz = BL_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "ledc timer");
    const ledc_channel_config_t ch = {
        .gpio_num = BSP_LCD_BACKLIGHT,
        .speed_mode = BL_MODE,
        .channel = BL_CHANNEL,
        .timer_sel = BL_TIMER,
        .duty = 0,
    };
    return ledc_channel_config(&ch);
}

void bsp_backlight_set(int percent)
{
    percent = percent < 0 ? 0 : percent > 100 ? 100 : percent;
    s_brightness = percent;
    // Perceived brightness is roughly the square root of duty, so square the percentage.
    // Any non-zero setting keeps at least one count on.
    uint32_t duty = (BL_DUTY_MAX * percent * percent + 9999) / 10000;
    ledc_set_duty(BL_MODE, BL_CHANNEL, duty);
    ledc_update_duty(BL_MODE, BL_CHANNEL);
}

int bsp_backlight_get(void)
{
    return s_brightness;
}

// Widen every invalidated area to 4-pixel boundaries. 320 and 480 are multiples of 4,
// so the result never leaves the screen.
static void rounder_cb(lv_event_t *e)
{
    lv_area_t *area = lv_event_get_param(e);
    area->x1 &= ~3;
    area->y1 &= ~3;
    area->x2 |= 3;
    area->y2 |= 3;
}

static esp_err_t panel_start(esp_lcd_panel_io_handle_t *io_out)
{
    const spi_bus_config_t bus = ST77922_PANEL_BUS_QSPI_CONFIG(
        BSP_LCD_CLK, BSP_LCD_D0, BSP_LCD_D1, BSP_LCD_D2, BSP_LCD_D3,
        BSP_LCD_H_RES * LCD_BUF_LINES * sizeof(uint16_t));
    ESP_RETURN_ON_ERROR(spi_bus_initialize(LCD_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "spi bus");

    esp_lcd_panel_io_spi_config_t io_cfg = ST77922_PANEL_IO_QSPI_CONFIG(BSP_LCD_CS, NULL, NULL);
    io_cfg.pclk_hz = LCD_PIXEL_HZ;
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_cfg, io_out),
                        TAG, "panel io");

    const st77922_vendor_config_t vendor = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags.use_qspi_interface = 1,
    };
    const esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = 16,
        .vendor_config = (void *)&vendor,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st77922(*io_out, &panel_cfg, &s_panel), TAG, "panel");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_panel), TAG, "reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_panel), TAG, "init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_panel, true), TAG, "invert");
    return esp_lcd_panel_disp_on_off(s_panel, true);
}

static lv_indev_t *touch_start(lv_display_t *disp)
{
    esp_lcd_panel_io_handle_t io = NULL;
    esp_lcd_panel_io_i2c_config_t io_cfg = ESP_LCD_TOUCH_IO_I2C_ST7123_CONFIG();
    io_cfg.scl_speed_hz = 400 * 1000;
    if (esp_lcd_new_panel_io_i2c(s_i2c, &io_cfg, &io) != ESP_OK) {
        return NULL;
    }
    // INT is left to the sleep code (light-sleep wake); LVGL polls the controller.
    const esp_lcd_touch_config_t tp_cfg = {
        .x_max = BSP_LCD_H_RES,
        .y_max = BSP_LCD_V_RES,
        .rst_gpio_num = GPIO_NUM_NC,
        .int_gpio_num = GPIO_NUM_NC,
    };
    esp_lcd_touch_handle_t tp = NULL;
    esp_err_t err = esp_lcd_touch_new_i2c_st7123(io, &tp_cfg, &tp);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "touch controller not responding at 0x%02X: %s",
                 ESP_LCD_TOUCH_IO_I2C_ST7123_ADDRESS, esp_err_to_name(err));
        return NULL;
    }
    const lvgl_port_touch_cfg_t cfg = {
        .disp = disp,
        .handle = tp,
    };
    return lvgl_port_add_touch(&cfg);
}

lv_display_t *bsp_display_start(void)
{
    if (s_disp) {
        return s_disp;
    }
    ESP_RETURN_ON_FALSE(backlight_start() == ESP_OK, NULL, TAG, "backlight init failed");
    ESP_RETURN_ON_FALSE(i2c_start() == ESP_OK, NULL, TAG, "I2C init failed");

    ESP_RETURN_ON_FALSE(panel_start(&s_io) == ESP_OK, NULL, TAG, "panel init failed");

    lvgl_port_cfg_t port_cfg = ESP_LVGL_PORT_INIT_CONFIG();
    port_cfg.task_stack = 16 * 1024;    // the app decodes JPEGs on the LVGL task
    ESP_RETURN_ON_FALSE(lvgl_port_init(&port_cfg) == ESP_OK, NULL, TAG, "lvgl_port_init failed");

    const lvgl_port_display_cfg_t disp_cfg = {
        .io_handle = s_io,
        .panel_handle = s_panel,
        .buffer_size = BSP_LCD_H_RES * LCD_BUF_LINES,
        .double_buffer = true,
        .hres = BSP_LCD_H_RES,
        .vres = BSP_LCD_V_RES,
        .monochrome = false,
        .color_format = LV_COLOR_FORMAT_RGB565,
        .rotation = {
            .swap_xy = false,
            .mirror_x = false,
            .mirror_y = false,
        },
        .flags = {
            .buff_dma = true,       // internal, DMA-capable RAM
            .buff_spiram = false,
            .swap_bytes = true,     // LVGL renders little-endian RGB565, the panel wants big-endian
        },
    };
    s_disp = lvgl_port_add_disp(&disp_cfg);
    ESP_RETURN_ON_FALSE(s_disp, NULL, TAG, "lvgl_port_add_disp failed");

    lvgl_port_lock(0);
    lv_display_add_event_cb(s_disp, rounder_cb, LV_EVENT_INVALIDATE_AREA, NULL);
    lvgl_port_unlock();

    s_touch = touch_start(s_disp);
    ESP_LOGI(TAG, "display %dx%d up, touch %s", BSP_LCD_H_RES, BSP_LCD_V_RES, s_touch ? "ok" : "MISSING");
    return s_disp;
}

// esp_lcd_st77922 has no disp_sleep hook, so send SLPIN/SLPOUT with the QSPI command
// framing the driver uses: opcode 0x02, then the register in bits 15..8 of the address.
#define QSPI_CMD(reg)   ((0x02 << 24) | ((reg) << 8))
#define LCD_SLPIN       0x10
#define LCD_SLPOUT      0x11

esp_err_t bsp_display_sleep(bool sleep)
{
    ESP_RETURN_ON_FALSE(s_io, ESP_ERR_INVALID_STATE, TAG, "display not started");
    esp_err_t err = esp_lcd_panel_io_tx_param(s_io, QSPI_CMD(sleep ? LCD_SLPIN : LCD_SLPOUT), NULL, 0);
    // SLPIN needs 5 ms before the clock may stop; SLPOUT needs 120 ms before the next command
    vTaskDelay(pdMS_TO_TICKS(sleep ? 10 : 120));
    return err;
}
