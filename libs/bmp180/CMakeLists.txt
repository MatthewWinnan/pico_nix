add_library(bmp180 STATIC
    src/bmp180.c
    src/bmp180_i2c.c
    src/i2c_config.c
)

target_include_directories(bmp180 PUBLIC include)

target_link_libraries(bmp180 PUBLIC
    pico_stdlib
    hardware_i2c
)
