# Recipe database, filters, pantry matching and user-state persistence.
#
# ESP-IDF: registered as a normal component (no JSON loader, no file storage;
# the firmware supplies a packed-binary loader and an NVS storage backend).
# Host (simulator, unit tests): add_subdirectory() this folder to get the
# static library `adb_data`, which includes the cJSON loader and file storage.

set(ADB_DATA_CORE_SRCS
    src/adb_tags.c
    src/adb_db.c
    src/adb_filter.c
    src/adb_pantry.c
    src/adb_storage.c)

if(ESP_PLATFORM)
    idf_component_register(SRCS ${ADB_DATA_CORE_SRCS}
                           INCLUDE_DIRS include)
    return()
endif()

if(NOT TARGET adb_cjson)
    include(FetchContent)
    # SOURCE_SUBDIR points at a folder without a CMakeLists.txt so only the
    # sources are fetched; we compile cJSON.c ourselves.
    FetchContent_Declare(cjson_src
        GIT_REPOSITORY https://github.com/DaveGamble/cJSON.git
        GIT_TAG v1.7.18
        GIT_SHALLOW TRUE
        SOURCE_SUBDIR do-not-build)
    FetchContent_MakeAvailable(cjson_src)
    add_library(adb_cjson STATIC ${cjson_src_SOURCE_DIR}/cJSON.c)
    target_include_directories(adb_cjson PUBLIC ${cjson_src_SOURCE_DIR})
    target_compile_options(adb_cjson PRIVATE -Wno-deprecated-declarations)
endif()

add_library(adb_data STATIC
    ${ADB_DATA_CORE_SRCS}
    src/adb_storage_file.c
    src/adb_load_json.c)
target_include_directories(adb_data PUBLIC ${CMAKE_CURRENT_LIST_DIR}/include)
target_link_libraries(adb_data PUBLIC adb_cjson m)
target_compile_options(adb_data PRIVATE -Wall -Wextra -Wno-unused-parameter)
