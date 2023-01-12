# Set minimum required version of CMake
cmake_minimum_required(VERSION 3.18)
#include build functions from Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)
# Set name of project (as PROJECT_NAME) and C/C++ Standards
project(SerialBuffer C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
# Creates a pico-sdk subdirectory in our project for the libraries
pico_sdk_init()
# point out the CMake, where to find the executable source file
message (STATUS "Project_name = ${PROJECT_NAME}")
add_executable(${PROJECT_NAME}
        main.c
        fifo.c
)
# Pull in our pico_stdlib which pulls in commonly used features (gpio, timer-delay etc)
target_link_libraries(${PROJECT_NAME}
        pico_stdlib hardware_uart hardware_gpio
)
# create map/bin/hex/uf2 files.
pico_add_extra_outputs(${PROJECT_NAME})
