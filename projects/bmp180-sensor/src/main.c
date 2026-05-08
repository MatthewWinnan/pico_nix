#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "bmp180.h"

// Standard sea-level pressure (Pa). Adjust if your local met service
// provides a more accurate reference for your area.
#define MY_SEA_LEVEL_PA   101325.0f

// Height of the sensor above sea level in metres. Set this to your
// location's elevation so sea-level pressure is calculated correctly.
#define MY_STATION_ALT_M  0.0f

int main(void) {
    stdio_init_all();

    // Wait for a USB CDC serial connection before printing anything.
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    printf("BMP180 Sensor Demo\r\n");
    printf("------------------\r\n");

    struct bmp180_calib_param  params;
    struct bmp180_measurements measures;
    struct bmp180_model        sensor;

    // ── I2C bus scan ────────────────────────────────────────────────────────
    // Scan the bus before init to confirm the BMP180 is reachable at 0x77.
    // Remove this block once the sensor is confirmed working.
    printf("Scanning I2C bus (SDA=GP%d, SCL=GP%d, port=i2c%d)...\r\n",
           GPIO_I2C0_SDA, GPIO_I2C0_SCL,
           (I2C_PORT == i2c0) ? 0 : 1);
    i2c_init(I2C_PORT, I2C_BAUDRATE);
    gpio_set_function(GPIO_I2C0_SDA, GPIO_FUNC_I2C);
    gpio_set_function(GPIO_I2C0_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(GPIO_I2C0_SDA);
    gpio_pull_up(GPIO_I2C0_SCL);

    bool found_any = false;
    for (uint8_t addr = 0x08; addr < 0x78; addr++) {
        uint8_t buf;
        int ret = i2c_read_blocking(I2C_PORT, addr, &buf, 1, false);
        if (ret >= 0) {
            printf("  Found device at 0x%02X%s\r\n",
                   addr, addr == BMP_180_ADDR ? " <-- BMP180" : "");
            found_any = true;
        }
    }
    if (!found_any) {
        printf("  No devices found — check wiring and pull-ups.\r\n");
    }
    printf("Scan complete.\r\n\r\n");
    // ── end scan ─────────────────────────────────────────────────────────────

    global_i2c_init();
    bmp180_init(&sensor, &params, &measures);

    printf("BMP180 initialised (chip ID: 0x%02X)\r\n\r\n", sensor.chipID);

    while (true) {
        // One measurement cycle for all four values.
        // bmp180_compute_* derive altitude and sea-level pressure from the
        // already-sampled p without triggering additional I2C reads.
        bmp180_get_measurement(&sensor);
        bmp180_compute_altitude(&sensor, MY_SEA_LEVEL_PA);
        bmp180_compute_sea_pressure(&sensor, MY_STATION_ALT_M);

        float temp_c       = (float)sensor.measurement_params->T / 10.0f;
        float pressure_hpa = (float)sensor.measurement_params->p / 100.0f;
        float altitude_m   = sensor.measurement_params->altitude;
        float sea_hpa      = sensor.measurement_params->p_relative / 100.0f;

        printf("Temp: %6.1f C  |  Pressure: %7.2f hPa  |  "
               "Altitude: %6.1f m  |  Sea-level P: %7.2f hPa\r\n",
               temp_c, pressure_hpa, altitude_m, sea_hpa);

        sleep_ms(1000);
    }
}
