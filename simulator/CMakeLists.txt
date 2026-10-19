# Desktop simulator for Aaron's Drink Butler: LVGL 9.4 + SDL2, 320x480.
#   GUI:      cmake -S simulator -B simulator/build && cmake --build simulator/build -j
#   headless: cmake -S simulator -B simulator/build-headless -DADB_SIM_HEADLESS=ON
# SDL2 is fetched and linked statically (no Homebrew SDL needed).
cmake_minimum_required(VERSION 3.16)
project(drink_butler_sim C CXX)
set(CMAKE_C_STANDARD 11)
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

get_filename_component(ADB_REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

option(ADB_SIM_HEADLESS "Build without SDL (screenshots, self-test, bench only)" OFF)

include(FetchContent)
if(ADB_SIM_HEADLESS)
    add_compile_definitions(ADB_SIM_HEADLESS=1)
    set(ADB_SDL_LIB "")
else()
    # LVGL 9.4's SDL driver speaks SDL2. Homebrew's "sdl2" is now sdl2-compat,
    # which dlopens SDL3 at startup, so build real SDL2 and link it statically.
    set(SDL_SHARED OFF CACHE BOOL "" FORCE)
    set(SDL_STATIC ON CACHE BOOL "" FORCE)
    set(SDL_TEST OFF CACHE BOOL "" FORCE)
    set(SDL2_DISABLE_INSTALL ON CACHE BOOL "" FORCE)
    FetchContent_Declare(sdl2
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG release-2.30.12
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(sdl2)
    set(ADB_SDL_LIB SDL2::SDL2-static)
endif()

# ---- LVGL ----------------------------------------------------------------
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/src/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(lvgl
    GIT_REPOSITORY https://github.com/lvgl/lvgl.git
    GIT_TAG v9.4.0
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(lvgl)
target_link_libraries(lvgl PUBLIC ${ADB_SDL_LIB})
# our fonts (adb_font_16 is LV_FONT_DEFAULT) are compiled into adb_ui
target_compile_options(lvgl PRIVATE -w)

# ---- App components --------------------------------------------------------
add_subdirectory(${ADB_REPO_DIR}/firmware/components/data ${CMAKE_BINARY_DIR}/adb_data)
add_subdirectory(${ADB_REPO_DIR}/firmware/components/ui ${CMAKE_BINARY_DIR}/adb_ui)

add_executable(drink_butler_sim
    src/main.c
    src/sim_platform.c
    src/screenshots.c)
target_include_directories(drink_butler_sim PRIVATE src ${lvgl_SOURCE_DIR}/src/libs/lodepng)
target_link_libraries(drink_butler_sim PRIVATE adb_ui adb_data lvgl ${ADB_SDL_LIB} m)
target_compile_definitions(drink_butler_sim PRIVATE ADB_REPO_DIR="${ADB_REPO_DIR}")
target_compile_options(drink_butler_sim PRIVATE -Wall -Wextra -Wno-unused-parameter)
# Note: lvgl refers to LV_FONT_DEFAULT (adb_font_16), which lives in adb_ui.
# Apple's linker resolves symbols across static archives in any order.
