cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(bmp180_sensor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

# Shared BMP180 driver library (path is relative to this CMakeLists.txt)
add_subdirectory(../../libs/bmp180 bmp180)

add_executable(bmp180_sensor
    src/main.c
)

target_link_libraries(bmp180_sensor
    pico_stdlib
    bmp180
)

pico_enable_stdio_usb(bmp180_sensor 1)
pico_enable_stdio_uart(bmp180_sensor 0)

pico_add_extra_outputs(bmp180_sensor)
