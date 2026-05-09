add_library(i2c0 STATIC
    src/i2c_config.c
)

target_include_directories(i2c0 PUBLIC include)

target_link_libraries(i2c0 PUBLIC
    pico_stdlib
    hardware_i2c
)
