#ifndef __BMP180_H__
#define __BMP180_H__

#include <stdio.h>
#include <math.h>
#include "bmp180_i2c.h"

/*
Data sheet: https://www.alldatasheet.com/view.jsp?Searchword=Bmp180%20Datasheet
References in comments use BMP180_DOC_<page_number>.

This driver communicates via I2C using the Pico SDK.
bmp180_i2c.h wraps the board-specific I2C calls.
i2c_config.h defines I2C port, pins, and the init function.
*/

// BMP180 Global Constants
#define BMP_180_CHIP_ID_ADDR    _u(0xD0)        // BMP180_DOC_18
#define BMP_180_CHIP_ID         _u(0x55)        // BMP180_DOC_18
#define BMP_180_ADDR            _u(0xEE >> 1)   // BMP180_DOC_20: 7-bit address

// Debug / info verbosity (0 = off, 1 = on)
#define BMP_180_DEBUG_MODE  0
#define BMP_180_INFO_MODE   0

// Global Register Values (BMP180_DOC_18)
#define BMP_180_REG_A1_MSB      _u(0xAA)
#define BMP_180_N_CAL_PARAMS    22

// Soft reset
#define BMP_180_REG_SOFT_RST    _u(0xE0)
#define BMP_180_POWER_ON_RESET  _u(0xB6)

// Measurement control register and trigger values (BMP180_DOC_21)
#define BMP_180_REG_CTRL_MEAS   _u(0xF4)
#define BMP_180_SET_TMP         _u(0x2E)
#define BMP_180_SET_PRESS_OSS_0 _u(0x34)
#define BMP_180_SET_PRESS_OSS_1 _u(0x74)
#define BMP_180_SET_PRESS_OSS_2 _u(0xB4)
#define BMP_180_SET_PRESS_OSS_3 _u(0xF4)
#define BMP_180_OSS             1   // Oversampling mode (0–3)
#define BMP_180_SS              3   // Samples to average per measurement

// Output registers
#define BMP_180_REG_OUT_MSB     _u(0xF6)
#define BMP_180_REG_OUT_LSB     _u(0xF7)
#define BMP_180_REG_OUT_XLSB    _u(0xF8)

// Conversion timing (ms), BMP180_DOC_21
#define BMP_180_TMP_TIME        5
#define BMP_180_PRES_OSS_0      5
#define BMP_180_PRES_OSS_1      8
#define BMP_180_PRES_OSS_2      14
#define BMP_180_PRES_OSS_3      26

// Calibration parameters (BMP180_DOC_18)
struct bmp180_calib_param {
    int16_t  AC1, AC2, AC3;
    uint16_t AC4, AC5, AC6;
    int16_t  B1, B2;
    int16_t  MB, MC, MD;
};

// Intermediate and final measurement values
struct bmp180_measurements {
    long ut, up;

    // Temperature intermediates
    long X1_tmp, X2_tmp;

    // Pressure intermediates
    long X1_p_1, X2_p_1, X3_p_1;
    long X1_p_2, X2_p_2, X3_p_2;
    long X1_p_3, X2_p_3, X1_p_4;
    long B3, B5, B6;
    unsigned long B4, B7;

    // Accumulation variables for averaging
    long T_sum, p_sum;

    // Final results
    long T;         // Temperature in 0.1 °C units (divide by 10.0 for °C)
    long p_inter;
    long p;         // Pressure in Pa

    // Derived
    float altitude;     // Altitude in metres
    float p_relative;   // Sea-level relative pressure in Pa
};

// Chip model: ties together calibration, measurements, and chip identity
struct bmp180_model {
    struct bmp180_calib_param    *cal_params;
    struct bmp180_measurements   *measurement_params;
    uint8_t chipID;
};

// Internal LUTs (defined in bmp180.c)
extern uint16_t pressure_oss[4];
extern uint16_t pressure_time[4];

// Public API
void bmp180_init(struct bmp180_model *my_chip,
                 struct bmp180_calib_param *my_params,
                 struct bmp180_measurements *measures);

// Take BMP_180_SS averaged samples and store final T (0.1 °C units) and p (Pa).
void bmp180_get_measurement(struct bmp180_model *my_chip);

// Compute altitude (m) and sea-level pressure (Pa) from the ALREADY-TAKEN
// measurement stored in my_chip->measurement_params->p.
// Call bmp180_get_measurement() first, then call these — no extra I2C traffic.
//   sea_level_pa : reference pressure at sea level (Pa), e.g. 101325
//   station_alt_m: height of the measuring station above sea level (m)
void bmp180_compute_altitude(struct bmp180_model *my_chip, float sea_level_pa);
void bmp180_compute_sea_pressure(struct bmp180_model *my_chip, float station_alt_m);

// Convenience wrappers: take a fresh measurement then compute.
// NOTE: each wrapper calls bmp180_get_measurement() internally, so calling
// both back-to-back performs two separate measurement cycles.  Prefer calling
// bmp180_get_measurement() once and then bmp180_compute_*() instead.
void bmp180_get_altitude(struct bmp180_model *my_chip, float sea_level_pa);
void bmp180_get_sea_pressure(struct bmp180_model *my_chip, float station_alt_m);

// Lower-level functions (exposed for flexibility)
void bmp180_get_ut(struct bmp180_model *my_chip);
void bmp180_get_temp(struct bmp180_model *my_chip);
void bmp180_get_up(struct bmp180_model *my_chip);
void bmp180_get_pressure(struct bmp180_model *my_chip);
void bmp180_get_temp_pressure(struct bmp180_model *my_chip);

#endif // __BMP180_H__
