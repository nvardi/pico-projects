cmake_minimum_required(VERSION 3.18)

# Pull in SDK (must be before project)
include(pico_sdk_import.cmake)

project(pico-projects C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

if (PICO_SDK_VERSION_STRING VERSION_LESS "1.3.0")
    message(FATAL_ERROR "Raspberry Pi Pico SDK version 1.3.0 (or later) required. Your version is ${PICO_SDK_VERSION_STRING}")
endif()

set(PICO_PROJECTS_PATH ${PROJECT_SOURCE_DIR})

# Initialize the SDK
pico_sdk_init()

#include(example_auto_set_url.cmake)
# Add projects
add_subdirectory(blink)
add_subdirectory(serialBuffer)
#add_subdirectory(serialBufferTest)

add_compile_options(-Wall
        -Wno-format          # int != int32_t as far as the compiler is concerned because gcc has int32_t as long int
        -Wno-unused-function # we have some for the docs that aren't called
        -Wno-maybe-uninitialized
        )
