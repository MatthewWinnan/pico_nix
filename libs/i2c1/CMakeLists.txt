add_library(i2c1 STATIC
    src/i2c1_config.c
)

target_include_directories(i2c1 PUBLIC include)

target_link_libraries(i2c1 PUBLIC
    pico_stdlib
    hardware_i2c
)
