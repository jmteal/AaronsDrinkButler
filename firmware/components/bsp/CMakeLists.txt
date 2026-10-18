idf_component_register(
    SRCS "bsp_display.c" "bsp_audio.c" "bsp_battery.c" "bsp_sd.c" "bsp_sleep.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_driver_i2c
    PRIV_REQUIRES esp_lcd esp_adc esp_driver_ledc esp_driver_spi esp_driver_i2s
                  esp_driver_sdmmc esp_driver_gpio fatfs sdmmc
)
