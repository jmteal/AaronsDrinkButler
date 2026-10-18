idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_driver_gpio esp_partition esp_timer
)
