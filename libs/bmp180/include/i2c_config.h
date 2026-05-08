#ifndef __I2C_CONFIG_H__
#define __I2C_CONFIG_H__
// I2C port, pin, and baudrate configuration for the Pico SDK.
// Adjust GPIO_I2C0_SDA / GPIO_I2C0_SCL to match your wiring.

#include <stdio.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define I2C_PORT        i2c0
#define I2C_BAUDRATE    200000  // 200 kHz

#define GPIO_I2C0_SDA   4
#define GPIO_I2C0_SCL   5

void global_i2c_init(void);

#endif // __I2C_CONFIG_H__
