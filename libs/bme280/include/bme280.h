#ifndef __BME280_H__
#define __BME280_H__

#include <stdio.h>
#include <math.h>
#include "bme280_i2c.h"

/*
Driver for the Bosch BME280 combined temperature / pressure / humidity sensor.
Ported from pico_main; bugs fixed and high-level API completed.

Data sheet: https://cdn-shop.adafruit.com/datasheets/BST-BME280_DS001-10.pdf
References in comments use BME280_DOC_<page_number>.

Breakout board schematic: https://www.mouser.com/datasheet/2/737/adafruit-bme280-humidity-barometric-pressure-tempe-740823.pdf
References use BME280_CIRCUIT_<page_number>.

Compensated output units:
  T  — int32_t  in 0.01 °C    (e.g. 2234  → 22.34 °C)
  P  — uint32_t Q24.8 Pa      (e.g. 24674867 / 256 → 96386 Pa → 963.86 hPa)
  H  — uint32_t Q22.10 %RH    (e.g. 47445 / 1024 → 46.33 %RH)
*/

// ── Device address ────────────────────────────────────────────────────────────
// SDO pin low  → 0x76 (default on the Adafruit breakout)
// SDO pin high → 0x77
#define BME_280_ADDR            _u(0x76)
#define BME_280_CHIP_ID_ADDR    _u(0xD0)
#define BME_280_CHIP_ID         _u(0x60)

// ── Mode ─────────────────────────────────────────────────────────────────────
// Sleep:  0b00   Forced: 0b01 (or 0b10)   Normal: 0b11
// Init leaves the chip in sleep; bme280_get_measurement() uses forced mode.
#define BME_280_MODE            _u(0b00)
#define BME_280_RESET_VALUE     _u(0xB6)

// ── Oversampling settings ─────────────────────────────────────────────────────
// Index selects one of the six oversampling entries (BME280_DOC_26/27).
// Bosch weather-station profile: x16 P, x2 T, x1 H — ~40 ms measurement time.
#define BME_280_OSRS_H_MODE     _u(1)   // x1  oversampling for humidity
#define BME_280_OSRS_P_MODE     _u(5)   // x16 oversampling for pressure
#define BME_280_OSRS_T_MODE     _u(2)   // x2  oversampling for temperature

#define BME_280_OSRS_H_1  _u(0b000)
#define BME_280_OSRS_H_2  _u(0b001)
#define BME_280_OSRS_H_3  _u(0b010)
#define BME_280_OSRS_H_4  _u(0b011)
#define BME_280_OSRS_H_5  _u(0b100)
#define BME_280_OSRS_H_6  _u(0b101)
extern uint8_t bme280_osrs_h_mode_array[6];

#define BME_280_OSRS_P_1  _u(0b000)
#define BME_280_OSRS_P_2  _u(0b001)
#define BME_280_OSRS_P_3  _u(0b010)
#define BME_280_OSRS_P_4  _u(0b011)
#define BME_280_OSRS_P_5  _u(0b100)
#define BME_280_OSRS_P_6  _u(0b101)
extern uint8_t bme280_osrs_p_mode_array[6];

#define BME_280_OSRS_T_1  _u(0b000)
#define BME_280_OSRS_T_2  _u(0b001)
#define BME_280_OSRS_T_3  _u(0b010)
#define BME_280_OSRS_T_4  _u(0b011)
#define BME_280_OSRS_T_5  _u(0b100)
#define BME_280_OSRS_T_6  _u(0b101)
extern uint8_t bme280_osrs_t_mode_array[6];

// ── Standby time (normal mode only) ──────────────────────────────────────────
#define BME_280_T_SB_MODE   _u(0)
#define BME_280_T_SB_1      _u(0b000)
#define BME_280_T_SB_2      _u(0b001)
#define BME_280_T_SB_3      _u(0b010)
#define BME_280_T_SB_4      _u(0b011)
#define BME_280_T_SB_5      _u(0b100)
#define BME_280_T_SB_6      _u(0b101)
#define BME_280_T_SB_7      _u(0b110)
#define BME_280_T_SB_8      _u(0b111)
extern uint8_t bme280_t_sb_mode_array[8];

// Standby times in µs (BME280_DOC_28)
#define BME_280_T_SB_TIMING_1    500
#define BME_280_T_SB_TIMING_2    62500
#define BME_280_T_SB_TIMING_3    125000
#define BME_280_T_SB_TIMING_4    250000
#define BME_280_T_SB_TIMING_5    500000
#define BME_280_T_SB_TIMING_6    1000000
#define BME_280_T_SB_TIMING_7    10000
#define BME_280_T_SB_TIMING_8    20000
extern uint32_t bme280_t_sb_timing_array[8];

// ── IIR filter ────────────────────────────────────────────────────────────────
#define BME_280_FILTER_MODE _u(0)
#define BME_280_FILTER_1    _u(0b000)
#define BME_280_FILTER_2    _u(0b001)
#define BME_280_FILTER_3    _u(0b010)
#define BME_280_FILTER_4    _u(0b011)
#define BME_280_FILTER_5    _u(0b100)
extern uint8_t bme280_filter_mode_array[5];

// SPI 3-wire enable — leave 0 for I2C
#define BME_280_SPI3W_EN    _u(0)

// ── Timing ────────────────────────────────────────────────────────────────────
#define BME_280_STARTUP_SCALE_SAFE  10
#define BME_280_STARTUP_T           _u(BME_280_STARTUP_SCALE_SAFE * 2)

// ── Debug/info verbosity ─────────────────────────────────────────────────────
#define BME_280_DEBUG_MODE  0   // register-level prints
#define BME_280_INFO_MODE   1   // init feedback

// ── Register map (BME280_DOC_22/25) ──────────────────────────────────────────
#define BME_280_REG_T1_LSB      _u(0x88)
#define BME_280_REG_T1_MSB      _u(0x89)
#define BME_280_REG_T2_LSB      _u(0x8A)
#define BME_280_REG_T2_MSB      _u(0x8B)
#define BME_280_REG_T3_LSB      _u(0x8C)
#define BME_280_REG_T3_MSB      _u(0x8D)

#define BME_280_REG_P1_LSB      _u(0x8E)
#define BME_280_REG_P1_MSB      _u(0x8F)
#define BME_280_REG_P2_LSB      _u(0x90)
#define BME_280_REG_P2_MSB      _u(0x91)
#define BME_280_REG_P3_LSB      _u(0x92)
#define BME_280_REG_P3_MSB      _u(0x93)
#define BME_280_REG_P4_LSB      _u(0x94)
#define BME_280_REG_P4_MSB      _u(0x95)
#define BME_280_REG_P5_LSB      _u(0x96)
#define BME_280_REG_P5_MSB      _u(0x97)
#define BME_280_REG_P6_LSB      _u(0x98)
#define BME_280_REG_P6_MSB      _u(0x99)
#define BME_280_REG_P7_LSB      _u(0x9A)
#define BME_280_REG_P7_MSB      _u(0x9B)
#define BME_280_REG_P8_LSB      _u(0x9C)
#define BME_280_REG_P8_MSB      _u(0x9D)
#define BME_280_REG_P9_LSB      _u(0x9E)
#define BME_280_REG_P9_MSB      _u(0x9F)

#define BME_280_REG_H1          _u(0xA1)
#define BME_280_REG_H2_LSB      _u(0xE1)
#define BME_280_REG_H2_MSB      _u(0xE2)
#define BME_280_REG_H3          _u(0xE3)
#define BME_280_REG_H4_LSB      _u(0xE4)
#define BME_280_REG_H4_MSB      _u(0xE5)
#define BME_280_REG_H5_LSB      _u(0xE5)
#define BME_280_REG_H5_MSB      _u(0xE6)
#define BME_280_REG_H6          _u(0xE7)

#define BME_280_N_CAL_PARAMS    _u(32)

#define BME_280_REG_CONFIG      _u(0xF5)
#define BME_280_REG_CTRL_MEAS   _u(0xF4)
#define BME_280_REG_STATUS      _u(0xF3)
#define BME_280_REG_CTRL_HUM    _u(0xF2)
#define BME_280_REG_RESET       _u(0xE0)

#define BME_280_REG_HUM_MSB     _u(0xFD)
#define BME_280_REG_TEMP_MSB    _u(0xFA)
#define BME_280_REG_PRESS_MSB   _u(0xF7)

// ── Structures ────────────────────────────────────────────────────────────────

struct bme280_calib_param {
    uint16_t dig_T1;
    int16_t  dig_T2;
    int16_t  dig_T3;

    uint16_t dig_P1;
    int16_t  dig_P2;
    int16_t  dig_P3;
    int16_t  dig_P4;
    int16_t  dig_P5;
    int16_t  dig_P6;
    int16_t  dig_P7;
    int16_t  dig_P8;
    int16_t  dig_P9;

    uint8_t  dig_H1;
    int16_t  dig_H2;
    uint8_t  dig_H3;
    int16_t  dig_H4;
    int16_t  dig_H5;
    int8_t   dig_H6;
};

struct bme280_settings {
    uint8_t mode;
    uint8_t osrs_h;
    uint8_t osrs_t;
    uint8_t osrs_p;
    uint8_t filter;
    uint8_t spi3w_en;
    uint8_t t_sb;
};

struct bme280_measurements {
    // Raw ADC values
    int32_t adc_T;
    int32_t adc_P;
    int32_t adc_H;

    // Fine temperature (shared between T, P, H compensation)
    int32_t t_fine;

    // Intermediate values for temperature compensation
    int32_t T_1, T_2, T_3, T_4;

    // Intermediate values for pressure compensation
    int64_t P_1, P_2, P_3, P_4, P_5, P_6, P_7, P_8, P_9, P_10, P_11, P_12;

    // Intermediate values for humidity compensation
    int32_t H_1, H_2, H_3, H_4, H_5, H_6, H_7, H_8, H_9, H_10;
    int32_t H_11, H_12, H_13, H_14;

    // Compensated outputs
    int32_t  T;  // 0.01 °C  (e.g. 2234 → 22.34 °C)
    uint32_t P;  // Q24.8 Pa (divide by 256 to get Pa)
    uint32_t H;  // Q22.10 %RH (divide by 1024 to get %RH)
};

struct bme280_model {
    struct bme280_calib_param    *cal_params;
    struct bme280_settings       *settings;
    struct bme280_measurements   *measure;
    uint8_t chipID;
};

// ── Return codes ──────────────────────────────────────────────────────────────
#define BME280_OK     0
#define BME280_SLEEP  1
#define BME280_BUSY   2

// ── Public API ────────────────────────────────────────────────────────────────

// Initialise chip: read ID, load calibration, apply config/oversampling settings.
void bme280_init(struct bme280_model *my_chip,
                 struct bme280_calib_param *params,
                 struct bme280_settings *settings,
                 struct bme280_measurements *meas);

// High-level convenience: trigger a forced-mode measurement, block until done,
// and populate my_chip->measure->T, P, H with compensated values.
// This is the primary function to call from a sensor loop.
void bme280_get_measurement(struct bme280_model *my_chip);

// ── Lower-level functions (exposed for advanced use) ──────────────────────────

void    read_bme280_chip_id(struct bme280_model *my_chip);
void    read_bme280_callibration_params(struct bme280_model *my_chip, struct bme280_calib_param *params);

void    bme280_set_config(struct bme280_model *my_chip);
uint8_t bme280_set_ctrl_meas(struct bme280_model *my_chip);
void    bme280_set_ctrl_hum(struct bme280_model *my_chip);

void    bme280_read_ctrl_meas(struct bme280_model *my_chip);
void    bme280_read_config(struct bme280_model *my_chip);
void    bme280_read_ctrl_hum(struct bme280_model *my_chip);
void    bme280_read_status(uint8_t *reg);
bool    bme280_is_doing_conversion(void);

uint8_t bme280_start_measurements(struct bme280_model *my_chip);
uint8_t bme280_get_uncompensated_measurements(struct bme280_model *my_chip);
void    bme280_get_compensated_measurements_blocked(struct bme280_model *my_chip);
uint8_t bme280_get_compensated_measurements_non_blocked(struct bme280_model *my_chip);

void bme280_compensate_temp(struct bme280_model *my_chip);
void bme280_compensate_press(struct bme280_model *my_chip);
void bme280_compensate_hum(struct bme280_model *my_chip);

#endif // __BME280_H__
