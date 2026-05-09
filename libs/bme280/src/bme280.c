#include "bme280.h"

// ── Lookup tables ─────────────────────────────────────────────────────────────

uint8_t  bme280_osrs_h_mode_array[6]  = {0,0,0,0,0,0};
uint8_t  bme280_osrs_p_mode_array[6]  = {0,0,0,0,0,0};
uint8_t  bme280_osrs_t_mode_array[6]  = {0,0,0,0,0,0};
uint8_t  bme280_t_sb_mode_array[8]    = {0,0,0,0,0,0,0,0};
uint8_t  bme280_filter_mode_array[5]  = {0,0,0,0,0};
uint32_t bme280_t_sb_timing_array[8]  = {0,0,0,0,0,0,0,0};

// ── Debug helpers ─────────────────────────────────────────────────────────────

#if BME_280_INFO_MODE
static void print_cal_params_bme280(struct bme280_model *my_chip) {
    struct bme280_calib_param *p = my_chip->cal_params;
    printf("[BME280] Calibration params:\r\n");
    printf("  T1=%-6u  T2=%-6d  T3=%-6d\r\n", p->dig_T1, p->dig_T2, p->dig_T3);
    printf("  P1=%-6u  P2=%-6d  P3=%-6d\r\n", p->dig_P1, p->dig_P2, p->dig_P3);
    printf("  P4=%-6d  P5=%-6d  P6=%-6d\r\n", p->dig_P4, p->dig_P5, p->dig_P6);
    printf("  P7=%-6d  P8=%-6d  P9=%-6d\r\n", p->dig_P7, p->dig_P8, p->dig_P9);
    printf("  H1=%-3u   H2=%-6d  H3=%-3u\r\n", p->dig_H1, p->dig_H2, p->dig_H3);
    printf("  H4=%-6d  H5=%-6d  H6=%-4d\r\n", p->dig_H4, p->dig_H5, p->dig_H6);
}
#endif

// ── Initialisation ────────────────────────────────────────────────────────────

void read_bme280_chip_id(struct bme280_model *my_chip) {
    uint8_t chipID[1] = {0};
    uint8_t addr = BME_280_CHIP_ID_ADDR;
    bme280_i2c_read(BME_280_ADDR, &addr, chipID, 1, false);

    if (chipID[0] != BME_280_CHIP_ID) {
        while (true) {
            #if BME_280_INFO_MODE
            printf("[BME280] ERROR: unexpected chip ID 0x%02X (expected 0x%02X).\r\n",
                   chipID[0], BME_280_CHIP_ID);
            #endif
            sleep_ms(5000);
        }
    }
    #if BME_280_INFO_MODE
    printf("[BME280] Chip ID OK: 0x%02X\r\n", chipID[0]);
    #endif
    my_chip->chipID = chipID[0];
}

void read_bme280_callibration_params(struct bme280_model *my_chip,
                                     struct bme280_calib_param *params) {
    // Temperature (0x88–0x8D, 6 bytes)
    uint8_t rx_temp[6] = {0};
    uint8_t addr = BME_280_REG_T1_LSB;
    bme280_i2c_read(BME_280_ADDR, &addr, rx_temp, 6, false);
    params->dig_T1 = (uint16_t)((rx_temp[1] << 8) | rx_temp[0]);
    params->dig_T2 = (int16_t) ((rx_temp[3] << 8) | rx_temp[2]);
    params->dig_T3 = (int16_t) ((rx_temp[5] << 8) | rx_temp[4]);

    // Pressure (0x8E–0x9F, 18 bytes)
    uint8_t rx_press[18] = {0};
    addr = BME_280_REG_P1_LSB;
    bme280_i2c_read(BME_280_ADDR, &addr, rx_press, 18, false);
    params->dig_P1 = (uint16_t)((rx_press[1]  << 8) | rx_press[0]);
    params->dig_P2 = (int16_t) ((rx_press[3]  << 8) | rx_press[2]);
    params->dig_P3 = (int16_t) ((rx_press[5]  << 8) | rx_press[4]);
    params->dig_P4 = (int16_t) ((rx_press[7]  << 8) | rx_press[6]);
    params->dig_P5 = (int16_t) ((rx_press[9]  << 8) | rx_press[8]);
    params->dig_P6 = (int16_t) ((rx_press[11] << 8) | rx_press[10]);
    params->dig_P7 = (int16_t) ((rx_press[13] << 8) | rx_press[12]);
    params->dig_P8 = (int16_t) ((rx_press[15] << 8) | rx_press[14]);
    params->dig_P9 = (int16_t) ((rx_press[17] << 8) | rx_press[16]);

    // Humidity — H1 is at a separate address (0xA1)
    uint8_t reg_h1[1];
    addr = BME_280_REG_H1;
    bme280_i2c_read(BME_280_ADDR, &addr, reg_h1, 1, false);
    params->dig_H1 = reg_h1[0];

    // H2–H6 are at 0xE1–0xE7 (7 bytes, burst read)
    // Layout per BME280_DOC_23:
    //   [0]=0xE1 H2[7:0]   [1]=0xE2 H2[15:8]
    //   [2]=0xE3 H3[7:0]
    //   [3]=0xE4 H4[11:4]  [4]=0xE5 H4[3:0] / H5[3:0]  [5]=0xE6 H5[11:4]
    //   [6]=0xE7 H6[7:0]
    uint8_t rx_hum[7] = {0};
    addr = BME_280_REG_H2_LSB;
    bme280_i2c_read(BME_280_ADDR, &addr, rx_hum, 7, false);

    params->dig_H2 = (int16_t) ((rx_hum[1] << 8) | rx_hum[0]);
    params->dig_H3 = rx_hum[2];
    params->dig_H4 = (int16_t) ((rx_hum[3] << 4) | (rx_hum[4] & 0x0F));
    // FIX: was (rx_hum[4] & 0xF0) << 4 | rx_hum[5] — bytes were swapped
    params->dig_H5 = (int16_t) ((rx_hum[5] << 4) | (rx_hum[4] >> 4));
    params->dig_H6 = (int8_t)  rx_hum[6];

    my_chip->cal_params = params;

    #if BME_280_INFO_MODE
    print_cal_params_bme280(my_chip);
    #endif
}

void bme280_init(struct bme280_model *my_chip,
                 struct bme280_calib_param *params,
                 struct bme280_settings *settings,
                 struct bme280_measurements *meas) {
    sleep_ms(BME_280_STARTUP_T);

    read_bme280_chip_id(my_chip);
    read_bme280_callibration_params(my_chip, params);

    #if BME_280_INFO_MODE
    printf("[BME280] Loading mode registers...\r\n");
    #endif

    bme280_osrs_h_mode_array[0] = BME_280_OSRS_H_1;
    bme280_osrs_h_mode_array[1] = BME_280_OSRS_H_2;
    bme280_osrs_h_mode_array[2] = BME_280_OSRS_H_3;
    bme280_osrs_h_mode_array[3] = BME_280_OSRS_H_4;
    bme280_osrs_h_mode_array[4] = BME_280_OSRS_H_5;
    bme280_osrs_h_mode_array[5] = BME_280_OSRS_H_6;

    bme280_osrs_p_mode_array[0] = BME_280_OSRS_P_1;
    bme280_osrs_p_mode_array[1] = BME_280_OSRS_P_2;
    bme280_osrs_p_mode_array[2] = BME_280_OSRS_P_3;
    bme280_osrs_p_mode_array[3] = BME_280_OSRS_P_4;
    bme280_osrs_p_mode_array[4] = BME_280_OSRS_P_5;
    bme280_osrs_p_mode_array[5] = BME_280_OSRS_P_6;

    bme280_osrs_t_mode_array[0] = BME_280_OSRS_T_1;
    bme280_osrs_t_mode_array[1] = BME_280_OSRS_T_2;
    bme280_osrs_t_mode_array[2] = BME_280_OSRS_T_3;
    bme280_osrs_t_mode_array[3] = BME_280_OSRS_T_4;
    bme280_osrs_t_mode_array[4] = BME_280_OSRS_T_5;
    bme280_osrs_t_mode_array[5] = BME_280_OSRS_T_6;

    bme280_t_sb_mode_array[0] = BME_280_T_SB_1;
    bme280_t_sb_mode_array[1] = BME_280_T_SB_2;
    bme280_t_sb_mode_array[2] = BME_280_T_SB_3;
    bme280_t_sb_mode_array[3] = BME_280_T_SB_4;
    bme280_t_sb_mode_array[4] = BME_280_T_SB_5;
    bme280_t_sb_mode_array[5] = BME_280_T_SB_6;
    bme280_t_sb_mode_array[6] = BME_280_T_SB_7;
    bme280_t_sb_mode_array[7] = BME_280_T_SB_8;

    bme280_filter_mode_array[0] = BME_280_FILTER_1;
    bme280_filter_mode_array[1] = BME_280_FILTER_2;
    bme280_filter_mode_array[2] = BME_280_FILTER_3;
    bme280_filter_mode_array[3] = BME_280_FILTER_4;
    bme280_filter_mode_array[4] = BME_280_FILTER_5;

    bme280_t_sb_timing_array[0] = BME_280_T_SB_TIMING_1;
    bme280_t_sb_timing_array[1] = BME_280_T_SB_TIMING_2;
    bme280_t_sb_timing_array[2] = BME_280_T_SB_TIMING_3;
    bme280_t_sb_timing_array[3] = BME_280_T_SB_TIMING_4;
    bme280_t_sb_timing_array[4] = BME_280_T_SB_TIMING_5;
    bme280_t_sb_timing_array[5] = BME_280_T_SB_TIMING_6;
    bme280_t_sb_timing_array[6] = BME_280_T_SB_TIMING_7;
    bme280_t_sb_timing_array[7] = BME_280_T_SB_TIMING_8;

    #if BME_280_INFO_MODE
    printf("[BME280] Mode registers loaded. Applying initial settings...\r\n");
    #endif

    // FIX: was BME_280_FILTER_MODE (wrong constant) — mode and filter are separate fields
    settings->mode     = BME_280_MODE;
    settings->osrs_h   = bme280_osrs_h_mode_array[BME_280_OSRS_H_MODE];
    // FIX: was bme280_osrs_h_mode_array for both p and t — use the correct arrays
    settings->osrs_p   = bme280_osrs_p_mode_array[BME_280_OSRS_P_MODE];
    settings->osrs_t   = bme280_osrs_t_mode_array[BME_280_OSRS_T_MODE];
    settings->filter   = bme280_filter_mode_array[BME_280_FILTER_MODE];
    settings->t_sb     = bme280_t_sb_mode_array[BME_280_T_SB_MODE];
    settings->spi3w_en = BME_280_SPI3W_EN;

    my_chip->settings = settings;
    my_chip->measure  = meas;

    bme280_set_config(my_chip);
    // ctrl_hum change only takes effect after a write to ctrl_meas, so
    // calling set_ctrl_hum is sufficient (it calls set_ctrl_meas internally).
    bme280_set_ctrl_hum(my_chip);

    #if BME_280_INFO_MODE
    printf("[BME280] Ready.\r\n");
    #endif
}

// ── Configuration setters ─────────────────────────────────────────────────────

void bme280_set_config(struct bme280_model *my_chip) {
    // config register can only be written reliably in sleep mode (BME280_DOC_27).
    // Read only the mode bits directly — bme280_read_ctrl_meas() would also
    // overwrite settings->osrs_p and osrs_t with the chip's reset values (0),
    // clobbering the oversampling settings we just configured.
    uint8_t addr = BME_280_REG_CTRL_MEAS;
    uint8_t reg[1] = {0};
    bme280_i2c_read(BME_280_ADDR, &addr, reg, 1, false);
    uint8_t tmp_mode = reg[0] & 0x03;
    uint8_t ctrl_result;

    if (tmp_mode != 0) {
        my_chip->settings->mode = 0;
        ctrl_result = bme280_set_ctrl_meas(my_chip);
        while (ctrl_result == BME280_BUSY) {
            sleep_us(100);
            ctrl_result = bme280_set_ctrl_meas(my_chip);
        }
    }

    uint8_t write_buffer[2];
    write_buffer[0] = BME_280_REG_CONFIG;
    write_buffer[1] = (uint8_t)(((my_chip->settings->t_sb    << 5) & 0xE0) |
                                ((my_chip->settings->filter   << 2) & 0x1C) |
                                 (my_chip->settings->spi3w_en & 0x01));
    bme280_i2c_write(BME_280_ADDR, write_buffer, 2, false);

    if (tmp_mode != 0) {
        my_chip->settings->mode = tmp_mode;
        ctrl_result = bme280_set_ctrl_meas(my_chip);
        while (ctrl_result == BME280_BUSY) {
            sleep_us(100);
            ctrl_result = bme280_set_ctrl_meas(my_chip);
        }
    }
}

uint8_t bme280_set_ctrl_meas(struct bme280_model *my_chip) {
    // Don't write if a measurement is in progress.
    if (bme280_is_doing_conversion()) {
        return BME280_BUSY;
    }
    uint8_t write_buffer[2];
    write_buffer[0] = BME_280_REG_CTRL_MEAS;
    write_buffer[1] = (uint8_t)(( my_chip->settings->mode   & 0x03)        |
                                ((my_chip->settings->osrs_p << 2) & 0x1C)  |
                                ((my_chip->settings->osrs_t << 5) & 0xE0));
    bme280_i2c_write(BME_280_ADDR, write_buffer, 2, false);
    return BME280_OK;
}

void bme280_set_ctrl_hum(struct bme280_model *my_chip) {
    // Changes to ctrl_hum only take effect after a write to ctrl_meas (BME280_DOC_25).
    uint8_t write_buffer[2];
    write_buffer[0] = BME_280_REG_CTRL_HUM;
    write_buffer[1] = (uint8_t)(my_chip->settings->osrs_h & 0x07);
    bme280_i2c_write(BME_280_ADDR, write_buffer, 2, false);

    uint8_t ctrl_result = bme280_set_ctrl_meas(my_chip);
    while (ctrl_result == BME280_BUSY) {
        sleep_us(100);
        ctrl_result = bme280_set_ctrl_meas(my_chip);
    }
}

// ── Configuration getters ─────────────────────────────────────────────────────

void bme280_read_ctrl_meas(struct bme280_model *my_chip) {
    uint8_t addr = BME_280_REG_CTRL_MEAS;
    uint8_t reg[1];
    bme280_i2c_read(BME_280_ADDR, &addr, reg, 1, false);

    #if BME_280_DEBUG_MODE
    printf("CTRL_MEAS = 0x%02X\r\n", reg[0]);
    #endif

    my_chip->settings->mode   = reg[0] & 0x03;
    my_chip->settings->osrs_p = (reg[0] >> 2) & 0x07;
    my_chip->settings->osrs_t = (reg[0] >> 5) & 0x07;
}

void bme280_read_config(struct bme280_model *my_chip) {
    uint8_t addr = BME_280_REG_CONFIG;
    uint8_t reg[1];
    bme280_i2c_read(BME_280_ADDR, &addr, reg, 1, false);

    #if BME_280_DEBUG_MODE
    printf("CONFIG = 0x%02X\r\n", reg[0]);
    #endif

    my_chip->settings->spi3w_en = reg[0] & 0x01;
    my_chip->settings->filter   = (reg[0] >> 2) & 0x07;
    my_chip->settings->t_sb     = (reg[0] >> 5) & 0x07;
}

void bme280_read_ctrl_hum(struct bme280_model *my_chip) {
    uint8_t addr = BME_280_REG_CTRL_HUM;
    uint8_t reg[1];
    bme280_i2c_read(BME_280_ADDR, &addr, reg, 1, false);

    #if BME_280_DEBUG_MODE
    printf("CTRL_HUM = 0x%02X\r\n", reg[0]);
    #endif

    my_chip->settings->osrs_h = reg[0] & 0x07;
}

void bme280_read_status(uint8_t *reg) {
    uint8_t addr = BME_280_REG_STATUS;
    bme280_i2c_read(BME_280_ADDR, &addr, reg, 1, false);
}

bool bme280_is_doing_conversion(void) {
    uint8_t status[1];
    bme280_read_status(status);

    #if BME_280_DEBUG_MODE
    printf("STATUS = 0x%02X\r\n", status[0]);
    #endif

    // FIX: was >> 2 (bit 2); the measuring flag is bit 3 (BME280_DOC_26)
    return ((status[0] >> 3) & 0x01) == 1;
}

// ── Measurement flow ──────────────────────────────────────────────────────────

uint8_t bme280_start_measurements(struct bme280_model *my_chip) {
    if (my_chip->settings->mode == 0b11) {
        // Normal mode: chip samples continuously, just confirm it isn't mid-conversion.
        return bme280_is_doing_conversion() ? BME280_BUSY : BME280_OK;
    }
    if (my_chip->settings->mode == 0b00) {
        #if BME_280_DEBUG_MODE
        printf("[BME280] WARNING: start_measurements called while in sleep mode.\r\n");
        #endif
        return BME280_SLEEP;
    }
    // Forced mode (0b01 or 0b10): write ctrl_meas to trigger a single measurement.
    return bme280_set_ctrl_meas(my_chip);
}

uint8_t bme280_get_uncompensated_measurements(struct bme280_model *my_chip) {
    // Burst read 0xF7–0xFE (8 bytes) as recommended by BME280_DOC_21 to ensure
    // the shadow register mechanism keeps all three values from the same sample.
    if (bme280_is_doing_conversion()) {
        return BME280_BUSY;
    }

    uint8_t addr = BME_280_REG_PRESS_MSB;
    uint8_t buf[8] = {0};
    bme280_i2c_read(BME_280_ADDR, &addr, buf, 8, false);

    my_chip->measure->adc_P = ((uint32_t)buf[0] << 12) | ((uint32_t)buf[1] << 4) | (buf[2] >> 4);
    my_chip->measure->adc_T = ((uint32_t)buf[3] << 12) | ((uint32_t)buf[4] << 4) | (buf[5] >> 4);
    my_chip->measure->adc_H = ((uint32_t)buf[6] <<  8) |  (uint32_t)buf[7];

    return BME280_OK;
}

void bme280_get_compensated_measurements_blocked(struct bme280_model *my_chip) {
    uint8_t status = bme280_start_measurements(my_chip);

    while (status == BME280_BUSY) {
        sleep_us(100);
        status = bme280_start_measurements(my_chip);
    }
    if (status == BME280_SLEEP) {
        return;
    }

    status = bme280_get_uncompensated_measurements(my_chip);
    while (status == BME280_BUSY) {
        sleep_us(100);
        status = bme280_get_uncompensated_measurements(my_chip);
    }

    bme280_compensate_temp(my_chip);
    bme280_compensate_press(my_chip);
    bme280_compensate_hum(my_chip);
}

uint8_t bme280_get_compensated_measurements_non_blocked(struct bme280_model *my_chip) {
    uint8_t status = bme280_start_measurements(my_chip);
    if (status != BME280_OK) return status;

    status = bme280_get_uncompensated_measurements(my_chip);
    if (status != BME280_OK) return status;

    bme280_compensate_temp(my_chip);
    bme280_compensate_press(my_chip);
    bme280_compensate_hum(my_chip);
    return BME280_OK;
}

// ── High-level convenience ────────────────────────────────────────────────────

void bme280_get_measurement(struct bme280_model *my_chip) {
    // Trigger a forced-mode measurement, block until done, then return to sleep.
    // After a forced measurement the chip resets to sleep automatically; we
    // mirror that in the settings struct for correctness.
    my_chip->settings->mode = 0b01;
    bme280_get_compensated_measurements_blocked(my_chip);
    my_chip->settings->mode = 0b00;
}

// ── Compensation formulae (BME280_DOC_23, Bosch reference implementation) ─────

// Returns T in 0.01 °C; sets t_fine for use by P and H compensation.
void bme280_compensate_temp(struct bme280_model *my_chip) {
    const int32_t temperature_min = -4000;
    const int32_t temperature_max =  8500;

    my_chip->measure->T_1 = (int32_t)((my_chip->measure->adc_T / 8) -
                                       ((int32_t)my_chip->cal_params->dig_T1 * 2));
    my_chip->measure->T_2 = (my_chip->measure->T_1 * (int32_t)my_chip->cal_params->dig_T2) / 2048;
    my_chip->measure->T_3 = (int32_t)((my_chip->measure->adc_T / 16) -
                                       (int32_t)my_chip->cal_params->dig_T1);
    my_chip->measure->T_4 = (((my_chip->measure->T_3 * my_chip->measure->T_3) / 4096) *
                               (int32_t)my_chip->cal_params->dig_T3) / 16384;

    my_chip->measure->t_fine = my_chip->measure->T_2 + my_chip->measure->T_4;
    int32_t temperature = (my_chip->measure->t_fine * 5 + 128) / 256;

    if      (temperature < temperature_min) my_chip->measure->T = temperature_min;
    else if (temperature > temperature_max) my_chip->measure->T = temperature_max;
    else                                    my_chip->measure->T = temperature;
}

// Returns P in Q24.8 Pa (divide by 256 to get Pa, by 25600 to get hPa).
void bme280_compensate_press(struct bme280_model *my_chip) {
    // Clamp bounds in Q24.8 Pa units (divide by 256 to get Pa).
    // 300 hPa = 30000 Pa → 7680000;  1100 hPa = 110000 Pa → 28160000.
    const uint32_t pressure_min = 7680000;
    const uint32_t pressure_max = 28160000;

    my_chip->measure->P_1  = (int64_t)my_chip->measure->t_fine - 128000;
    my_chip->measure->P_2  = my_chip->measure->P_1 * my_chip->measure->P_1 *
                              (int64_t)my_chip->cal_params->dig_P6;
    my_chip->measure->P_3  = my_chip->measure->P_2 +
                             ((my_chip->measure->P_1 * (int64_t)my_chip->cal_params->dig_P5) * 131072);
    my_chip->measure->P_4  = my_chip->measure->P_3 +
                             ((int64_t)my_chip->cal_params->dig_P4 * 34359738368LL);
    my_chip->measure->P_5  = ((my_chip->measure->P_1 * my_chip->measure->P_1 *
                               (int64_t)my_chip->cal_params->dig_P3) / 256) +
                              (my_chip->measure->P_1 * (int64_t)my_chip->cal_params->dig_P2 * 4096);
    my_chip->measure->P_6  = (int64_t)140737488355328LL;
    my_chip->measure->P_7  = (my_chip->measure->P_6 + my_chip->measure->P_5) *
                              (int64_t)my_chip->cal_params->dig_P1 / 8589934592LL;

    if (my_chip->measure->P_7 == 0) {
        my_chip->measure->P = pressure_min;
        return;
    }

    my_chip->measure->P_8  = 1048576 - my_chip->measure->adc_P;
    my_chip->measure->P_9  = (((my_chip->measure->P_8 * INT64_C(2147483648)) -
                                 my_chip->measure->P_4) * 3125) / my_chip->measure->P_7;
    my_chip->measure->P_10 = ((int64_t)my_chip->cal_params->dig_P9 *
                              (my_chip->measure->P_9 / 8192) *
                              (my_chip->measure->P_9 / 8192)) / 33554432;
    my_chip->measure->P_11 = ((int64_t)my_chip->cal_params->dig_P8 *
                               my_chip->measure->P_9) / 524288;
    my_chip->measure->P_12 = ((my_chip->measure->P_9 +
                                my_chip->measure->P_10 +
                                my_chip->measure->P_11) / 256) +
                              ((int64_t)my_chip->cal_params->dig_P7 * 16);

    // P_12 is already Q24.8 Pa from the Bosch 64-bit formula — store it directly.
    uint32_t pressure = (uint32_t)my_chip->measure->P_12;

    if      (pressure < pressure_min) my_chip->measure->P = pressure_min;
    else if (pressure > pressure_max) my_chip->measure->P = pressure_max;
    else                              my_chip->measure->P = pressure;
}

// Returns H in Q22.10 %RH (divide by 1024 to get %RH).
void bme280_compensate_hum(struct bme280_model *my_chip) {
    const uint32_t humidity_max = 102400;

    my_chip->measure->H_1  = my_chip->measure->t_fine - (int32_t)76800;
    my_chip->measure->H_2  = (int32_t)(my_chip->measure->adc_H * 16384);
    my_chip->measure->H_3  = (int32_t)((int32_t)my_chip->cal_params->dig_H4 * 1048576);
    my_chip->measure->H_4  = (int32_t)my_chip->cal_params->dig_H5 * my_chip->measure->H_1;
    my_chip->measure->H_5  = ((my_chip->measure->H_2 -
                                my_chip->measure->H_3 -
                                my_chip->measure->H_4) + (int32_t)16384) / 32768;
    my_chip->measure->H_6  = (my_chip->measure->H_1 * (int32_t)my_chip->cal_params->dig_H6) / 1024;
    my_chip->measure->H_7  = (my_chip->measure->H_1 * (int32_t)my_chip->cal_params->dig_H3) / 2048;
    my_chip->measure->H_8  = ((my_chip->measure->H_6 *
                               (my_chip->measure->H_7 + (int32_t)32768)) / 1024) + (int32_t)2097152;
    my_chip->measure->H_9  = ((my_chip->measure->H_8 * (int32_t)my_chip->cal_params->dig_H2) +
                               8192) / 16384;
    my_chip->measure->H_10 = my_chip->measure->H_5 * my_chip->measure->H_9;
    my_chip->measure->H_11 = ((my_chip->measure->H_10 / 32768) *
                               (my_chip->measure->H_10 / 32768)) / 128;
    my_chip->measure->H_12 = my_chip->measure->H_10 -
                             ((my_chip->measure->H_11 * (int32_t)my_chip->cal_params->dig_H1) / 16);
    my_chip->measure->H_13 = (my_chip->measure->H_12 < 0) ? 0 : my_chip->measure->H_12;
    my_chip->measure->H_14 = (my_chip->measure->H_13 > 419430400) ? 419430400 : my_chip->measure->H_13;

    uint32_t humidity = (uint32_t)(my_chip->measure->H_14 / 4096);
    my_chip->measure->H = (humidity > humidity_max) ? humidity_max : humidity;
}
