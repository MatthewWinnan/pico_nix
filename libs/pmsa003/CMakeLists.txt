add_library(pmsa003 STATIC
    src/pmsa003.c
)

target_include_directories(pmsa003 PUBLIC include)

target_link_libraries(pmsa003 PUBLIC
    pico_stdlib
    hardware_uart
)
