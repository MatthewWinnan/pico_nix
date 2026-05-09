cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_w_air_sensor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

# Shared driver libraries (paths relative to this CMakeLists.txt)
add_subdirectory(../../libs/pmsa003 pmsa003)
add_subdirectory(../../libs/ssd1306 ssd1306)

add_executable(pico_w_air_sensor
    src/main.c
    src/provisioning.c
    src/mqtt_ha.c
)

# Project root contains lwipopts.h — must be on the include path so
# that pico_lwip_mqtt can find it when building the lwIP MQTT app layer.
target_include_directories(pico_w_air_sensor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pico_w_air_sensor
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    hardware_flash
    hardware_sync
    hardware_watchdog
    pmsa003
    ssd1306
)

# UART0 is used by the PMSA003 sensor — route stdio over USB instead
pico_enable_stdio_usb(pico_w_air_sensor 1)
pico_enable_stdio_uart(pico_w_air_sensor 0)

pico_add_extra_outputs(pico_w_air_sensor)
