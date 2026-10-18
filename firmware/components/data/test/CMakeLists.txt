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
target_compile_definitions(adb_data_tests PRIVATE
    SAMPLE_JSON="${CMAKE_CURRENT_LIST_DIR}/../../../../docs/research/sample_recipes.json")

enable_testing()
add_test(NAME adb_data_tests COMMAND adb_data_tests)
