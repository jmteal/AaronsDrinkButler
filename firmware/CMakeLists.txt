# Aaron's Drink Butler firmware (ESP-IDF 5.5, target esp32s3)
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
# Build only what main pulls in (no Wi-Fi, TLS, ...)
idf_build_set_property(MINIMAL_BUILD ON)
project(drink_butler)

# esp_audio_codec adds an unquoted "-L <its dir>/lib/<target>" link flag, which breaks when
# the checkout path has a space (".../Standalone Projects/..."). Its libraries are linked by
# full path anyway, so drop the flag.
idf_component_get_property(acodec_lib espressif__esp_audio_codec COMPONENT_LIB)
foreach(prop LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
    get_target_property(libs ${acodec_lib} ${prop})
    if(libs)
        list(FILTER libs EXCLUDE REGEX "-L ")
        set_target_properties(${acodec_lib} PROPERTIES ${prop} "${libs}")
    endif()
endforeach()

# Recipe pack for the read-only "drinks" partition. `idf.py drinks-flash` packs ../data
# (tools/data/build.py pack, standard library only) into build/drinks.bin and writes it
# to the partition. `idf.py flash` leaves the partition alone, so app reflashes stay fast.
idf_build_get_property(python PYTHON)
set(ADB_PACK_BIN "${CMAKE_BINARY_DIR}/drinks.bin")
add_custom_target(drinks_pack
    COMMAND ${python} "${CMAKE_SOURCE_DIR}/../tools/data/build.py" pack -o "${ADB_PACK_BIN}"
    BYPRODUCTS "${ADB_PACK_BIN}"
    VERBATIM)
idf_component_get_property(main_args esptool_py FLASH_ARGS)
idf_component_get_property(sub_args esptool_py FLASH_SUB_ARGS)
esptool_py_flash_target(drinks-flash "${main_args}" "${sub_args}" ALWAYS_PLAINTEXT)
esptool_py_flash_to_partition(drinks-flash drinks "${ADB_PACK_BIN}")
add_dependencies(drinks-flash drinks_pack)
