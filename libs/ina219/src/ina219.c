#include "ina219.h"

// ---------------------------------------------------------------------------
// Low-level register I/O (INA219 uses big-endian 16-bit registers)
// ---------------------------------------------------------------------------

static bool write_reg(uint8_t reg, uint16_t val) {
    uint8_t buf[3] = { reg, (uint8_t)(val >> 8), (uint8_t)(val & 0xFF) };
    return i2c_write_blocking(INA219_I2C_PORT, INA219_ADDR, buf, 3, false) == 3;
}

static bool read_reg(uint8_t reg, uint16_t *out) {
    if (i2c_write_blocking(INA219_I2C_PORT, INA219_ADDR, &reg, 1, true) != 1)
        return false;
    uint8_t buf[2];
    if (i2c_read_blocking(INA219_I2C_PORT, INA219_ADDR, buf, 2, false) != 2)
        return false;
    *out = ((uint16_t)buf[0] << 8) | buf[1];
    return true;
}

// ---------------------------------------------------------------------------
// Battery percentage from LiPo discharge curve
// ---------------------------------------------------------------------------

// Piecewise-linear approximation of a typical 14500 Li-ion discharge curve.
// Points are ordered highest voltage first.
static const struct { float v; int pct; } lipo_curve[] = {
    { 4.20f, 100 },
    { 4.05f,  90 },
    { 3.95f,  80 },
    { 3.85f,  70 },
    { 3.75f,  55 },
    { 3.65f,  40 },
    { 3.55f,  25 },
    { 3.40f,  10 },
    { 3.20f,   5 },
    { 3.00f,   0 },
};
static const int LIPO_CURVE_LEN =
    (int)(sizeof(lipo_curve) / sizeof(lipo_curve[0]));

static int voltage_to_percent(float v) {
    if (v >= lipo_curve[0].v)             return 100;
    if (v <= lipo_curve[LIPO_CURVE_LEN - 1].v) return 0;
    for (int i = 0; i < LIPO_CURVE_LEN - 1; i++) {
        if (v >= lipo_curve[i + 1].v) {
            float span = lipo_curve[i].v - lipo_curve[i + 1].v;
            float t    = (v - lipo_curve[i + 1].v) / span;
            return lipo_curve[i + 1].pct +
                   (int)(t * (float)(lipo_curve[i].pct - lipo_curve[i + 1].pct));
        }
    }
    return 0;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void ina219_init(void) {
    i2c_init(INA219_I2C_PORT, INA219_I2C_BAUD);
    gpio_set_function(INA219_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(INA219_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(INA219_SDA_PIN);
    gpio_pull_up(INA219_SCL_PIN);

    // Calibration must be written before current/power registers are valid.
    write_reg(INA219_REG_CALIB,  INA219_CALIB_VALUE);
    write_reg(INA219_REG_CONFIG, INA219_CONFIG_VALUE);
}

bool ina219_read(ina219_reading_t *out) {
    uint16_t bus_raw, shunt_raw, current_raw, power_raw;

    if (!read_reg(INA219_REG_BUS,     &bus_raw))     return false;
    if (!read_reg(INA219_REG_SHUNT,   &shunt_raw))   return false;
    if (!read_reg(INA219_REG_CURRENT, &current_raw)) return false;
    if (!read_reg(INA219_REG_POWER,   &power_raw))   return false;

    // Bus voltage: bits [15:3] are the result, LSB = 4 mV
    out->voltage_v  = (float)(bus_raw >> 3) * 0.004f;

    // Current: signed 16-bit, LSB = 1 mA (with Cal = 4096, R = 10 mΩ)
    out->current_ma = (float)(int16_t)current_raw;

    // Power: unsigned 16-bit, LSB = 20 mW
    out->power_mw   = (float)power_raw * 20.0f;

    out->percent    = voltage_to_percent(out->voltage_v);

    return true;
}
