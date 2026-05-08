#include "../include/i2c_config.h"

void global_i2c_init(void)
{
    i2c_init(I2C_PORT, I2C_BAUDRATE);
    gpio_set_function(GPIO_I2C0_SDA, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_I2C0_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_I2C0_SDA);
    gpio_pull_up(GPIO_I2C0_SCL);
}
