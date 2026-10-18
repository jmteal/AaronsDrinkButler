/*
 * Battery voltage on GPIO8 (ADC1_CH7) through a 100k/100k divider: Vbat = 2 x Vadc.
 * With no battery attached, this reads the TP4054 charger output (about 4.2 V on USB).
 * USB presence and charge state are not wired to any GPIO.
 */
#include "bsp.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_check.h"
#include "esp_log.h"

#define SAMPLES     32

static const char *TAG = "bsp_batt";

static adc_oneshot_unit_handle_t s_adc;
static adc_cali_handle_t s_cali;
static adc_channel_t s_channel;

esp_err_t bsp_battery_init(void)
{
    if (s_adc) {
        return ESP_OK;
    }
    adc_unit_t unit;
    ESP_RETURN_ON_ERROR(adc_oneshot_io_to_channel(BSP_BATTERY_ADC, &unit, &s_channel), TAG, "io->channel");

    const adc_oneshot_unit_init_cfg_t unit_cfg = {
        .unit_id = unit,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_new_unit(&unit_cfg, &s_adc), TAG, "adc unit");
    const adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ESP_RETURN_ON_ERROR(adc_oneshot_config_channel(s_adc, s_channel, &chan_cfg), TAG, "adc channel");

    const adc_cali_curve_fitting_config_t cali_cfg = {
        .unit_id = unit,
        .chan = s_channel,
        .atten = ADC_ATTEN_DB_12,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    if (adc_cali_create_scheme_curve_fitting(&cali_cfg, &s_cali) != ESP_OK) {
        s_cali = NULL;
        ESP_LOGW(TAG, "no eFuse ADC calibration; readings are uncalibrated");
    }
    return ESP_OK;
}

int bsp_battery_mv(void)
{
    if (!s_adc) {
        return -1;
    }
    int sum = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(s_adc, s_channel, &raw) != ESP_OK) {
            return -1;
        }
        int mv = 0;
        if (s_cali) {
            adc_cali_raw_to_voltage(s_cali, raw, &mv);
        } else {
            mv = raw * 3100 / 4095;     // rough full-scale at 12 dB
        }
        sum += mv;
    }
    return 2 * sum / SAMPLES;
}

/*
 * Resting 1S LiPo discharge curve (mV -> %), rough. Under load the cell sags 50-100 mV,
 * so readings taken with the backlight on run a little low.
 */
static const struct {
    int mv;
    int pct;
} s_curve[] = {
    {4200, 100}, {4100, 90}, {4000, 78}, {3900, 67}, {3850, 60}, {3800, 50},
    {3750, 40}, {3700, 32}, {3650, 22}, {3600, 14}, {3500, 7}, {3400, 0},
};

int bsp_battery_percent(int mv)
{
    const int n = sizeof(s_curve) / sizeof(s_curve[0]);
    if (mv >= s_curve[0].mv) {
        return 100;
    }
    if (mv <= s_curve[n - 1].mv) {
        return 0;
    }
    for (int i = 1; i < n; i++) {
        if (mv >= s_curve[i].mv) {
            int dv = s_curve[i - 1].mv - s_curve[i].mv;
            int dp = s_curve[i - 1].pct - s_curve[i].pct;
            return s_curve[i].pct + (mv - s_curve[i].mv) * dp / dv;
        }
    }
    return 0;
}
