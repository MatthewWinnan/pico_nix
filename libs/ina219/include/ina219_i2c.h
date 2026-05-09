#ifndef INA219_I2C_H
#define INA219_I2C_H
// MCU-specific I2C wrapper for the INA219.
// Swap this file's implementation for another platform's I2C API as needed.
// Bus configuration (port, pins, baudrate) lives in libs/i2c1/include/i2c1_config.h.

#include <stdbool.h>
#include <stdint.h>
#include "i2c1_config.h"

// Initialise I2C1. Delegates to global_i2c1_init().
void ina219_i2c_init(void);

// Write a 16-bit big-endian register value to device_addr.
// Returns true on success.
bool ina219_i2c_write_reg(uint8_t device_addr, uint8_t reg, uint16_t val);

// Read a 16-bit big-endian register value from device_addr into *out.
// Returns true on success.
bool ina219_i2c_read_reg(uint8_t device_addr, uint8_t reg, uint16_t *out);

#endif // INA219_I2C_H
