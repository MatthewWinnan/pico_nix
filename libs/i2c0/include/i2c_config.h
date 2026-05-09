#ifndef __I2C_CONFIG_H__
#define __I2C_CONFIG_H__
// Shared I2C0 bus configuration for all sensors on GP4/GP5.
// bmp180 and bme280 both live on this bus.
// Swap port/pins here to re-target the whole sensor cluster.

#include <stdio.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define I2C_PORT        i2c0
#define I2C_BAUDRATE    200000  // 200 kHz

#define GPIO_I2C0_SDA   4
#define GPIO_I2C0_SCL   5

// Initialise I2C0 and configure GP4/GP5. Call once from main before
// using any I2C0 sensor. bme280 does not re-init the bus.
void global_i2c_init(void);

#endif // __I2C_CONFIG_H__
