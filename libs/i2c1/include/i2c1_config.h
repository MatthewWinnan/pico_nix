#ifndef __I2C1_CONFIG_H__
#define __I2C1_CONFIG_H__
// Shared I2C1 bus configuration for all sensors on GP6/GP7.
// Currently used by the INA219 (Waveshare Pico-UPS-A).
// Swap port/pins here to re-target the whole I2C1 sensor cluster.

#include <stdio.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"

#define I2C1_PORT        i2c1
#define I2C1_BAUDRATE    400000  // 400 kHz

#define GPIO_I2C1_SDA    6
#define GPIO_I2C1_SCL    7

// Initialise I2C1 and configure GP6/GP7. Call once from main (or from the
// sensor's own init) before using any I2C1 device.
void global_i2c1_init(void);

#endif // __I2C1_CONFIG_H__
