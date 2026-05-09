#ifndef INA219_H
#define INA219_H

/*
 * Driver for the Texas Instruments INA219 current/power monitor.
 * Datasheet: docs/datasheets/ina219-datasheet.pdf
 *
 * Wiring on Waveshare Pico-UPS-A (from schematic):
 *   SDA → GP6  (I2C1)
 *   SCL → GP7  (I2C1)
 *   I2C address: 0x43  (confirmed via bus scan; schematic shows A0 tied high)
 *   Shunt resistor: 10 mΩ (R1 = 0.01 Ω, 1%, 2512 package)
 *
 * Calibration (Cal = 4096):
 *   Current_LSB = 0.04096 / (Cal × R_shunt) = 1 mA per LSB
 *   Power_LSB   = 20 × Current_LSB           = 20 mW per LSB
 */

#include <stdbool.h>
#include "ina219_i2c.h"

// I2C address — confirmed 0x43 via bus scan (A0=VCC, A1=GND on Pico-UPS-A)
#define INA219_ADDR      0x43

// Register map
#define INA219_REG_CONFIG   0x00
#define INA219_REG_SHUNT    0x01
#define INA219_REG_BUS      0x02
#define INA219_REG_POWER    0x03
#define INA219_REG_CURRENT  0x04
#define INA219_REG_CALIB    0x05

// Config: 32V bus range, ±320 mV shunt, 12-bit ADC, continuous mode (0x399F)
#define INA219_CONFIG_VALUE  0x399F

// Calibration for 10 mΩ shunt → 1 mA / LSB
#define INA219_CALIB_VALUE   4096

typedef struct {
    float voltage_v;   // Battery bus voltage (V)
    float current_ma;  // Charge/discharge current (mA); positive = charging
    float power_mw;    // Power (mW)
    int   percent;     // Estimated remaining capacity (0–100 %) from LiPo curve
} ina219_reading_t;

// Initialise I2C1 and write calibration / config registers.
void ina219_init(void);

// Read all four values into *out. Returns false on I2C error.
bool ina219_read(ina219_reading_t *out);

#endif // INA219_H
