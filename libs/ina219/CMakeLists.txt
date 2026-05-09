add_library(ina219 STATIC
    src/ina219.c
    src/ina219_i2c.c
)

target_include_directories(ina219 PUBLIC include)

target_link_libraries(ina219 PUBLIC
    i2c1
)
