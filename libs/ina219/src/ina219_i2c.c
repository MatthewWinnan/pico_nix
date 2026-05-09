#include "ina219_i2c.h"

void ina219_i2c_init(void) {
    global_i2c1_init();
}

bool ina219_i2c_write_reg(uint8_t device_addr, uint8_t reg, uint16_t val) {
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return i2c_write_blocking(I2C1_PORT, device_addr, buf, 3, false) == 3;
}

bool ina219_i2c_read_reg(uint8_t device_addr, uint8_t reg, uint16_t *out) {
    if (i2c_write_blocking(I2C1_PORT, device_addr, &reg, 1, true) != 1)
        return false;
    uint8_t buf[2];
    if (i2c_read_blocking(I2C1_PORT, device_addr, buf, 2, false) != 2)
        return false;
    *out = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}
