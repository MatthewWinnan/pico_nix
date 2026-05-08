#ifndef __BMP180_I2C__
#define __BMP180_I2C__
// Thin wrapper around the Pico SDK I2C calls.
// Swap this file's implementation for another MCU's I2C API as needed.

#include <stdio.h>
#include "pico/stdlib.h"
#include "i2c_config.h"

int bmp180_i2c_write(uint8_t addr, const uint8_t *src, size_t len, bool nostop);
int bmp180_i2c_read(uint8_t addr, const uint8_t *src, uint8_t *dst, size_t len, bool nostop);

#endif // __BMP180_I2C__
