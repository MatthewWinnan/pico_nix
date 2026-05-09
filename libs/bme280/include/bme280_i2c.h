#ifndef __BME280_I2C__
#define __BME280_I2C__
// Thin wrapper around the Pico SDK I2C calls for the BME280.
// Swap this file's implementation for another MCU's I2C API as needed.
//
// Note from BME280 datasheet:
//   Multi-byte write: each byte requires its own register address (no auto-increment).
//   Multi-byte read:  single start address, then auto-incremented burst read.

#include <stdio.h>
#include "pico/stdlib.h"
#include "i2c_config.h"

int bme280_i2c_write(uint8_t addr, const uint8_t *src, size_t len, bool nostop);
int bme280_i2c_read(uint8_t addr, const uint8_t *src, uint8_t *dst, size_t len, bool nostop);

#endif // __BME280_I2C__
