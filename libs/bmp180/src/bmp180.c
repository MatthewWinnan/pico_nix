#include "../include/bmp180.h"

uint16_t pressure_oss[4]  = {0, 0, 0, 0};
uint16_t pressure_time[4] = {0, 0, 0, 0};

static void bmp180_get_cal(struct bmp180_calib_param *params, struct bmp180_model *my_chip)
{
    uint8_t out_buff[BMP_180_N_CAL_PARAMS] = {0};
    uint8_t reg_start = BMP_180_REG_A1_MSB;

    bmp180_i2c_read(BMP_180_ADDR, &reg_start, out_buff, BMP_180_N_CAL_PARAMS, false);

    params->AC1 = (int16_t)  ((out_buff[0]  << 8) | out_buff[1]);
    params->AC2 = (int16_t)  ((out_buff[2]  << 8) | out_buff[3]);
    params->AC3 = (int16_t)  ((out_buff[4]  << 8) | out_buff[5]);
    params->AC4 = (uint16_t) ((out_buff[6]  << 8) | out_buff[7]);
    params->AC5 = (uint16_t) ((out_buff[8]  << 8) | out_buff[9]);
    params->AC6 = (uint16_t) ((out_buff[10] << 8) | out_buff[11]);
    params->B1  = (int16_t)  ((out_buff[12] << 8) | out_buff[13]);
    params->B2  = (int16_t)  ((out_buff[14] << 8) | out_buff[15]);
    params->MB  = (int16_t)  ((out_buff[16] << 8) | out_buff[17]);
    params->MC  = (int16_t)  ((out_buff[18] << 8) | out_buff[19]);
    params->MD  = (int16_t)  ((out_buff[20] << 8) | out_buff[21]);

    my_chip->cal_params = params;
}

void bmp180_init(struct bmp180_model *my_chip,
                 struct bmp180_calib_param *my_params,
                 struct bmp180_measurements *measures)
{
    pressure_oss[0]  = BMP_180_SET_PRESS_OSS_0;
    pressure_oss[1]  = BMP_180_SET_PRESS_OSS_1;
    pressure_oss[2]  = BMP_180_SET_PRESS_OSS_2;
    pressure_oss[3]  = BMP_180_SET_PRESS_OSS_3;
    pressure_time[0] = BMP_180_PRES_OSS_0;
    pressure_time[1] = BMP_180_PRES_OSS_1;
    pressure_time[2] = BMP_180_PRES_OSS_2;
    pressure_time[3] = BMP_180_PRES_OSS_3;

    // BMP180 needs ~10 ms to start; 1 s is conservative but reliable.
    sleep_ms(1000);

    uint8_t chipID[1];
    uint8_t addr = BMP_180_CHIP_ID_ADDR;
    bmp180_i2c_read(BMP_180_ADDR, &addr, chipID, 1, false);

    if (chipID[0] != BMP_180_CHIP_ID) {
        while (true) {
            printf("BMP180 not found (got 0x%02X, expected 0x%02X)\r\n",
                   chipID[0], BMP_180_CHIP_ID);
            sleep_ms(5000);
        }
    }

    my_chip->chipID = chipID[0];
    my_chip->measurement_params = measures;

    bmp180_get_cal(my_params, my_chip);
}

void bmp180_get_ut(struct bmp180_model *my_chip)
{
    uint8_t write_buff[2] = {BMP_180_REG_CTRL_MEAS, BMP_180_SET_TMP};
    uint8_t read_buff[2];

    bmp180_i2c_write(BMP_180_ADDR, write_buff, 2, false);
    sleep_ms(BMP_180_TMP_TIME * 2);

    uint8_t addr = BMP_180_REG_OUT_MSB;
    bmp180_i2c_read(BMP_180_ADDR, &addr, read_buff, 2, false);

    my_chip->measurement_params->ut = (read_buff[0] << 8) | read_buff[1];
}

void bmp180_get_temp(struct bmp180_model *my_chip)
{
    bmp180_get_ut(my_chip);

    my_chip->measurement_params->X1_tmp =
        ((my_chip->measurement_params->ut - my_chip->cal_params->AC6) *
          my_chip->cal_params->AC5) >> 15;

    my_chip->measurement_params->X2_tmp =
        (my_chip->cal_params->MC << 11) /
        (my_chip->measurement_params->X1_tmp + my_chip->cal_params->MD);

    my_chip->measurement_params->B5 =
        my_chip->measurement_params->X1_tmp + my_chip->measurement_params->X2_tmp;

    my_chip->measurement_params->T_sum +=
        (my_chip->measurement_params->B5 + 8) >> 4;
}

void bmp180_get_up(struct bmp180_model *my_chip)
{
    uint8_t write_buff[2] = {BMP_180_REG_CTRL_MEAS, pressure_oss[BMP_180_OSS]};
    uint8_t read_buff[3];

    bmp180_i2c_write(BMP_180_ADDR, write_buff, 2, false);
    sleep_ms(pressure_time[BMP_180_OSS] * 3);

    uint8_t addr = BMP_180_REG_OUT_MSB;
    bmp180_i2c_read(BMP_180_ADDR, &addr, read_buff, 3, false);

    my_chip->measurement_params->up =
        ((read_buff[0] << 16) | (read_buff[1] << 8) | read_buff[2]) >> (8 - BMP_180_OSS);
}

void bmp180_get_pressure(struct bmp180_model *my_chip)
{
    bmp180_get_up(my_chip);

    my_chip->measurement_params->B6 = my_chip->measurement_params->B5 - 4000;

    long B6_int = (long)((float)my_chip->measurement_params->B6 *
                          ((float)my_chip->measurement_params->B6 / powf(2.0f, 12.0f)));

    my_chip->measurement_params->X1_p_1 =
        (my_chip->cal_params->B2 * B6_int) >> 11;

    my_chip->measurement_params->X2_p_1 =
        (long)((float)my_chip->cal_params->AC2 *
               ((float)my_chip->measurement_params->B6 / powf(2.0f, 11.0f)));

    my_chip->measurement_params->X3_p_1 =
        my_chip->measurement_params->X1_p_1 + my_chip->measurement_params->X2_p_1;

    my_chip->measurement_params->B3 =
        ((((long)(my_chip->cal_params->AC1 << 2) + my_chip->measurement_params->X3_p_1)
          << BMP_180_OSS) + 2) >> 2;

    my_chip->measurement_params->X1_p_2 =
        (long)((float)my_chip->cal_params->AC3 *
               ((float)my_chip->measurement_params->B6 / powf(2.0f, 13.0f)));

    my_chip->measurement_params->X2_p_2 =
        (long)((float)my_chip->cal_params->B1 * (float)B6_int / powf(2.0f, 16.0f));

    my_chip->measurement_params->X3_p_2 =
        ((my_chip->measurement_params->X1_p_2 + my_chip->measurement_params->X2_p_2) + 2) >> 2;

    my_chip->measurement_params->B4 =
        (unsigned long)((float)my_chip->cal_params->AC4 *
                        ((float)((unsigned long)(my_chip->measurement_params->X3_p_2 + 32768)) /
                         powf(2.0f, 15.0f)));

    my_chip->measurement_params->B7 =
        (unsigned long)((float)((unsigned long)my_chip->measurement_params->up -
                                 my_chip->measurement_params->B3) *
                        (50000.0f / powf(2.0f, (float)BMP_180_OSS)));

    if (my_chip->measurement_params->B7 < 0x80000000) {
        my_chip->measurement_params->p_inter =
            (my_chip->measurement_params->B7 << 1) / my_chip->measurement_params->B4;
    } else {
        my_chip->measurement_params->p_inter =
            (my_chip->measurement_params->B7 / my_chip->measurement_params->B4) << 1;
    }

    my_chip->measurement_params->X1_p_3 =
        (long)(((float)my_chip->measurement_params->p_inter / powf(2.0f, 8.0f)) *
               ((float)my_chip->measurement_params->p_inter / powf(2.0f, 8.0f)));

    my_chip->measurement_params->X1_p_4 =
        (long)((float)my_chip->measurement_params->X1_p_3 * 3038.0f / powf(2.0f, 16.0f));

    my_chip->measurement_params->X2_p_3 =
        (long)(-7357.0f * (float)my_chip->measurement_params->p_inter / powf(2.0f, 16.0f));

    my_chip->measurement_params->p_sum +=
        my_chip->measurement_params->p_inter +
        (long)((float)(my_chip->measurement_params->X1_p_4 +
                        my_chip->measurement_params->X2_p_3 + 3791) /
               powf(2.0f, 4.0f));
}

void bmp180_get_temp_pressure(struct bmp180_model *my_chip)
{
    bmp180_get_temp(my_chip);
    bmp180_get_pressure(my_chip);
}

void bmp180_get_measurement(struct bmp180_model *my_chip)
{
    my_chip->measurement_params->p_sum = 0;
    my_chip->measurement_params->T_sum = 0;

    for (uint8_t i = 0; i < BMP_180_SS; i++) {
        bmp180_get_temp(my_chip);
        bmp180_get_pressure(my_chip);
    }

    my_chip->measurement_params->p = (long)(my_chip->measurement_params->p_sum / BMP_180_SS);
    my_chip->measurement_params->T = (long)(my_chip->measurement_params->T_sum / BMP_180_SS);
}

// Compute altitude from an already-taken measurement (no I2C).
// Uses the international barometric formula (BMP180 datasheet section 3.6).
void bmp180_compute_altitude(struct bmp180_model *my_chip, float sea_level_pa)
{
    float p_ratio = (float)my_chip->measurement_params->p / sea_level_pa;
    my_chip->measurement_params->altitude = 44330.0f * (1.0f - powf(p_ratio, 1.0f / 5.255f));
}

// Compute sea-level relative pressure from an already-taken measurement (no I2C).
// station_alt_m is the elevation of the sensor above sea level in metres.
void bmp180_compute_sea_pressure(struct bmp180_model *my_chip, float station_alt_m)
{
    my_chip->measurement_params->p_relative =
        (float)my_chip->measurement_params->p /
        powf(1.0f - (station_alt_m / 44330.0f), 5.255f);
}

// Convenience wrappers: measure then compute.
void bmp180_get_altitude(struct bmp180_model *my_chip, float sea_level_pa)
{
    bmp180_get_measurement(my_chip);
    bmp180_compute_altitude(my_chip, sea_level_pa);
}

void bmp180_get_sea_pressure(struct bmp180_model *my_chip, float station_alt_m)
{
    bmp180_get_measurement(my_chip);
    bmp180_compute_sea_pressure(my_chip, station_alt_m);
}
