#include "../include/bmp180_i2c.h"

int bmp180_i2c_write(uint8_t addr, const uint8_t *src, size_t len, bool nostop)
{
    int result = i2c_write_blocking(I2C_PORT, addr, src, len, nostop);
    if (result == PICO_ERROR_GENERIC) {
        printf("BMP180: I2C write to 0x%02X failed\r\n", addr);
    }
    return result;
}

int bmp180_i2c_read(uint8_t addr, const uint8_t *src, uint8_t *dst, size_t len, bool nostop)
{
    // Write the register address first, retaining bus control (nostop=true).
    int result = bmp180_i2c_write(addr, src, 1, true);
    if (result == PICO_ERROR_GENERIC) {
        return result;
    }

    result = i2c_read_blocking(I2C_PORT, addr, dst, len, nostop);
    if (result == PICO_ERROR_GENERIC) {
        printf("BMP180: I2C read from reg 0x%02X failed\r\n", *src);
    }
    return result;
}
