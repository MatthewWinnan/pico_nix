#include "bme280_i2c.h"

int bme280_i2c_write(uint8_t addr, const uint8_t *src, size_t len, bool nostop) {
    int answer = i2c_write_blocking(I2C_PORT, addr, src, len, nostop);
    if (answer == PICO_ERROR_GENERIC) {
        printf("BME280 Write to addr 0x%02X FAILED with PICO_ERROR_GENERIC.\r\n", addr);
    }
    return answer;
}

int bme280_i2c_read(uint8_t addr, const uint8_t *src, uint8_t *dst, size_t len, bool nostop) {
    // Write the register address we want to read from, keeping the bus
    int answer = bme280_i2c_write(addr, src, 1, true);
    if (answer == PICO_ERROR_GENERIC) {
        return answer;
    }
    answer = i2c_read_blocking(I2C_PORT, addr, dst, len, nostop);
    if (answer == PICO_ERROR_GENERIC) {
        printf("BME280 Read from reg 0x%02X FAILED with PICO_ERROR_GENERIC.\r\n", *src);
    }
    return answer;
}
