# Host unit tests for the data component.
#   cmake -S firmware/components/data/test -B build/data-test && cmake --build build/data-test
#   ctest --test-dir build/data-test --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(adb_data_tests C)
set(CMAKE_C_STANDARD 11)

add_subdirectory(.. adb_data)

add_executable(adb_data_tests test_data.c)
target_link_libraries(adb_data_tests PRIVATE adb_data)
target_compile_options(adb_data_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
get_filename_component(ADB_REPO_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
target_compile_definitions(adb_data_tests PRIVATE
    SAMPLE_JSON="${ADB_REPO_DIR}/docs/research/sample_recipes.json"
    PACK_BIN="${CMAKE_CURRENT_BINARY_DIR}/drinks.bin")

# The full dataset as a device pack, written by the same tool that makes the flash image.
# With ADB_TEST_AUDIO (default) the pack has the spoken recipes and the test checks every
# drink has one; that runs the TTS (instant once tools/data/.cache/tts is warm).
option(ADB_TEST_AUDIO "Pack and check the spoken recipes" ON)
if(ADB_TEST_AUDIO)
    set(ADB_PACK_FLAGS "")
else()
    set(ADB_PACK_FLAGS "--no-audio")
endif()
target_compile_definitions(adb_data_tests PRIVATE PACK_AUDIO=$<BOOL:${ADB_TEST_AUDIO}>)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
add_custom_target(test_pack ALL
    COMMAND ${Python3_EXECUTABLE} ${ADB_REPO_DIR}/tools/data/build.py pack ${ADB_PACK_FLAGS} -o ${CMAKE_CURRENT_BINARY_DIR}/drinks.bin
    COMMENT "Packing data/ for the pack test")

enable_testing()
add_test(NAME adb_data_tests COMMAND adb_data_tests)
