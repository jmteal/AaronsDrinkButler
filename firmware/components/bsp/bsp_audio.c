/*
 * Speaker output: ES8311 codec (I2C 0x18) over I2S, into the SC8002B amplifier.
 * The amp's SHUTDOWN pin is GPIO1 (low = on, 10k pull-up). The BSP drives it itself
 * rather than letting esp_codec_dev do it, so the amp is only on while a clip plays.
 * The microphone path is not used.
 */
#include "bsp.h"

#include <string.h>

#include "driver/i2s_std.h"
#include "es8311_codec.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#define I2S_PORT        I2S_NUM_0
#define AMP_ON_LEVEL    0
#define AMP_OFF_LEVEL   1
#define AMP_SETTLE_MS   30      // amp output settles before audio starts (fewer pops)
#define TAIL_MS         60      // silence written after a clip, flushes the DMA buffers

static const char *TAG = "bsp_audio";

static esp_codec_dev_handle_t s_codec;
static SemaphoreHandle_t s_lock;
static int16_t silence[BSP_AUDIO_SAMPLE_RATE * TAIL_MS / 1000];

static void amp_set(bool on)
{
    gpio_hold_dis(BSP_AMP_ENABLE);
    gpio_set_level(BSP_AMP_ENABLE, on ? AMP_ON_LEVEL : AMP_OFF_LEVEL);
}

esp_err_t bsp_audio_init(void)
{
    if (s_codec) {
        return ESP_OK;
    }
    i2c_master_bus_handle_t bus = bsp_i2c_bus();
    ESP_RETURN_ON_FALSE(bus, ESP_ERR_INVALID_STATE, TAG, "call bsp_display_start() first");

    // Amp off first: a deep-sleep wake may have left the pin held.
    gpio_hold_dis(BSP_AMP_ENABLE);
    gpio_deep_sleep_hold_dis();
    const gpio_config_t amp_cfg = {
        .pin_bit_mask = BIT64(BSP_AMP_ENABLE),
        .mode = GPIO_MODE_OUTPUT,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&amp_cfg), TAG, "amp gpio");
    amp_set(false);

    i2s_chan_handle_t tx = NULL;
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.auto_clear_after_cb = true;    // play silence, not stale data, when starved
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &tx, NULL), TAG, "i2s channel");
    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BSP_AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_I2S_MCLK,
            .bclk = BSP_I2S_BCLK,
            .ws = BSP_I2S_WS,
            .dout = BSP_I2S_DOUT,
            .din = I2S_GPIO_UNUSED,
        },
    };
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(tx, &std_cfg), TAG, "i2s std mode");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(tx), TAG, "i2s enable");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = I2S_PORT,
        .tx_handle = tx,
    };
    const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = I2C_NUM_0,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = bus,
    };
    const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(data_if && ctrl_if && gpio_if, ESP_FAIL, TAG, "codec interfaces");

    es8311_codec_cfg_t es_cfg = {
        .ctrl_if = ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = -1,           // amp handled by amp_set()
        .use_mclk = true,
        .hw_gain = {
            .pa_voltage = 5.0,
            .codec_dac_voltage = 3.3,
        },
    };
    const audio_codec_if_t *codec_if = es8311_codec_new(&es_cfg);
    ESP_RETURN_ON_FALSE(codec_if, ESP_ERR_NOT_FOUND, TAG, "ES8311 not responding at 0x%02X",
                        ES8311_CODEC_DEFAULT_ADDR >> 1);

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = codec_if,
        .data_if = data_if,
    };
    s_codec = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_codec, ESP_FAIL, TAG, "esp_codec_dev_new");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BSP_AUDIO_SAMPLE_RATE,
        .channel = 1,
        .bits_per_sample = 16,
    };
    ESP_RETURN_ON_FALSE(esp_codec_dev_open(s_codec, &fs) == ESP_CODEC_DEV_OK, ESP_FAIL, TAG, "codec open");
    esp_codec_dev_set_out_vol(s_codec, 60);

    s_lock = xSemaphoreCreateMutex();
    ESP_LOGI(TAG, "ES8311 ready, %d Hz mono", BSP_AUDIO_SAMPLE_RATE);
    return ESP_OK;
}

esp_err_t bsp_audio_set_volume(int volume)
{
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_INVALID_STATE, TAG, "audio not started");
    volume = volume < 0 ? 0 : volume > 100 ? 100 : volume;
    return esp_codec_dev_set_out_vol(s_codec, volume) == ESP_CODEC_DEV_OK ? ESP_OK : ESP_FAIL;
}

esp_err_t bsp_audio_play(const int16_t *pcm, size_t samples)
{
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_INVALID_STATE, TAG, "audio not started");
    ESP_RETURN_ON_FALSE(pcm || !samples, ESP_ERR_INVALID_ARG, TAG, "no samples");

    esp_err_t err = ESP_OK;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    amp_set(true);
    vTaskDelay(pdMS_TO_TICKS(AMP_SETTLE_MS));
    // esp_codec_dev_write blocks until the data is queued for DMA; the trailing silence
    // pushes the last real samples out before the amp switches off.
    if (esp_codec_dev_write(s_codec, (void *)pcm, samples * sizeof(int16_t)) != ESP_CODEC_DEV_OK ||
        esp_codec_dev_write(s_codec, silence, sizeof(silence)) != ESP_CODEC_DEV_OK) {
        err = ESP_FAIL;
    }
    amp_set(false);
    xSemaphoreGive(s_lock);
    return err;
}

esp_err_t bsp_audio_stream_begin(uint32_t timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_codec, ESP_ERR_INVALID_STATE, TAG, "audio not started");
    if (xSemaphoreTake(s_lock, pdMS_TO_TICKS(timeout_ms)) != pdTRUE) return ESP_ERR_TIMEOUT;
    amp_set(true);
    vTaskDelay(pdMS_TO_TICKS(AMP_SETTLE_MS));
    return ESP_OK;
}

esp_err_t bsp_audio_stream_write(const int16_t *pcm, size_t samples)
{
    return esp_codec_dev_write(s_codec, (void *)pcm, samples * sizeof(int16_t)) == ESP_CODEC_DEV_OK
               ? ESP_OK : ESP_FAIL;
}

void bsp_audio_stream_end(void)
{
    esp_codec_dev_write(s_codec, silence, sizeof(silence));
    amp_set(false);
    xSemaphoreGive(s_lock);
}
