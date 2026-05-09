#include "i2c1_config.h"

void global_i2c1_init(void) {
    i2c_init(I2C1_PORT, I2C1_BAUDRATE);
    gpio_set_function(GPIO_I2C1_SDA, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_I2C1_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_I2C1_SDA);
    gpio_pull_up(GPIO_I2C1_SCL);
}
