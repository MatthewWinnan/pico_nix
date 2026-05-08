add_library(ina219 STATIC
    src/ina219.c
)

target_include_directories(ina219 PUBLIC include)

target_link_libraries(ina219 PUBLIC
    pico_stdlib
    hardware_i2c
)
