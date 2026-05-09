add_library(bme280 STATIC
    src/bme280.c
    src/bme280_i2c.c
)

target_include_directories(bme280 PUBLIC include)

target_link_libraries(bme280 PUBLIC
    i2c0
)
