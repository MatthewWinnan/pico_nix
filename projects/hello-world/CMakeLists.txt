cmake_minimum_required(VERSION 3.13)

# Must be included before project() — pulls in the Pico SDK toolchain file.
# PICO_SDK_PATH is set by the Nix pico-sdk setup hook.
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(hello_world C CXX ASM)
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

pico_sdk_init()

# Generate compile_commands.json for clangd LSP
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(hello_world
    src/main.c
)

target_link_libraries(hello_world
    pico_stdlib
)

# Route stdio over USB CDC (shows up as /dev/ttyACM* after boot).
# TinyUSB is available because pico-sdk is built with withSubmodules = true in the flake.
pico_enable_stdio_usb(hello_world 1)
pico_enable_stdio_uart(hello_world 0)

# Generate .uf2, .bin, .hex, .map alongside the .elf
pico_add_extra_outputs(hello_world)
