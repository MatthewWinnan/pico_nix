add_library(ssd1306 STATIC
    src/ssd1306.c
)

target_include_directories(ssd1306 PUBLIC include)

target_link_libraries(ssd1306 PUBLIC
    pico_stdlib
    hardware_i2c
)
