# LVGL screens for Aaron's Drink Butler, shared by the firmware and the
# desktop simulator.
#
# ESP-IDF: a normal component; needs the `lvgl` component (pulled in by the
# firmware through esp_lvgl_port) and our `data` component.
# Host: add_subdirectory() this folder after creating the `lvgl` and
# `adb_data` targets to get the static library `adb_ui`.

file(GLOB ADB_UI_SRCS ${CMAKE_CURRENT_LIST_DIR}/src/*.c ${CMAKE_CURRENT_LIST_DIR}/fonts/*.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${ADB_UI_SRCS}
                           INCLUDE_DIRS include
                           PRIV_INCLUDE_DIRS src
                           REQUIRES lvgl data)
    return()
endif()

add_library(adb_ui STATIC ${ADB_UI_SRCS})
target_include_directories(adb_ui PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include
                                  PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src)
target_link_libraries(adb_ui PUBLIC lvgl adb_data m)
target_compile_options(adb_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
