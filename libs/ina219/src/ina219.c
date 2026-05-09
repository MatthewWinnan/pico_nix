#include "ina219.h"

// ---------------------------------------------------------------------------
// Low-level register I/O (INA219 uses big-endian 16-bit registers)
// ---------------------------------------------------------------------------

static bool write_reg(uint8_t reg, uint16_t val) {
    return ina219_i2c_write_reg(INA219_ADDR, reg, val);
}

static bool read_reg(uint8_t reg, uint16_t *out) {
    return ina219_i2c_read_reg(INA219_ADDR, reg, out);
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
    ina219_i2c_init();

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
