# Aaron's Drink Butler firmware (ESP-IDF 5.5, target esp32s3)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only what main pulls in (no Wi-Fi, TLS, ...)
idf_build_set_property(MINIMAL_BUILD ON)
project(drink_butler)
