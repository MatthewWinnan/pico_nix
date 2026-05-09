cmake_minimum_required(VERSION 3.13)

set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(pico_w_env_sensor C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

pico_sdk_init()

# Shared drivers (relative to this CMakeLists.txt)
add_subdirectory(../../libs/i2c0    i2c0)
add_subdirectory(../../libs/i2c1    i2c1)
add_subdirectory(../../libs/bmp180  bmp180)
add_subdirectory(../../libs/bme280  bme280)
add_subdirectory(../../libs/ina219  ina219)

add_executable(pico_w_env_sensor
    src/main.c
    src/provisioning.c
    src/mqtt_ha.c
)

# Project root contains lwipopts.h — must be on the include path so
# that pico_lwip_mqtt can find it when building the lwIP MQTT app layer.
target_include_directories(pico_w_env_sensor PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(pico_w_env_sensor
    pico_stdlib
    pico_cyw43_arch_lwip_threadsafe_background
    pico_lwip_mqtt
    hardware_flash
    hardware_sync
    hardware_watchdog
    bmp180
    bme280
    ina219
)

# Pico W uses USB for stdio; UART is routed through the CYW43 on some builds.
pico_enable_stdio_usb(pico_w_env_sensor 1)
pico_enable_stdio_uart(pico_w_env_sensor 0)

pico_add_extra_outputs(pico_w_env_sensor)
