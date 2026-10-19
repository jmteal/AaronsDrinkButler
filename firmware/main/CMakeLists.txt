if(CONFIG_ADB_HWTEST)
    set(srcs "hwtest.c")
else()
    set(srcs "app_main.c" "app_images.c" "app_storage_nvs.c" "app_platform.c" "app_console.c"
             "app_speech.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "."
    REQUIRES bsp data ui esp_driver_gpio esp_partition esp_timer nvs_flash esp_driver_usb_serial_jtag
    PRIV_REQUIRES esp_app_format json espressif__esp_audio_codec
)
