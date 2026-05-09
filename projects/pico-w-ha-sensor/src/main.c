#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"

#include "bmp180.h"
#include "bme280.h"
#include "ina219.h"
#include "provisioning.h"
#include "mqtt_ha.h"

// Fallback station altitude used if no GPS fix has been received yet.
// Set to the approximate elevation of your location (metres above sea level).
#define FALLBACK_STATION_ALT_M  1457.8f

// Sensor measurement interval in milliseconds
#define MEASURE_INTERVAL_MS  10000

// EMA smoothing factor for QNH (sea-level pressure).
// α=0.05 → ~20-sample window (~3 min at 10 s interval) — very smooth.
// α=0.20 → ~5-sample window  (~50 s) — tracks fronts faster, less GPS jitter.
#define QNH_EMA_ALPHA  0.2f

// ---------------------------------------------------------------------------
// WiFi connect with retry (Pico W / CYW43)
// ---------------------------------------------------------------------------

static bool wifi_connect(const char *ssid, const char *pass) {
    printf("Connecting to WiFi '%s'...\r\n", ssid);
    int ret = cyw43_arch_wifi_connect_timeout_ms(
                  ssid, pass, CYW43_AUTH_WPA2_AES_PSK, 30000);
    if (ret == 0) {
        printf("WiFi connected.\r\n");
        return true;
    }
    printf("WiFi connect failed (error %d).\r\n", ret);
    return false;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
    stdio_init_all();

    // Wait for a USB CDC terminal before printing anything.
    while (!stdio_usb_connected()) {
        sleep_ms(100);
    }

    printf("\r\npico-w-ha-sensor starting...\r\n");

    // ── Credentials ──────────────────────────────────────────────────────────
    creds_t creds;
    if (!creds_load(&creds)) {
        creds_provision(&creds);
    } else {
        printf("Loaded credentials from flash (SSID: %s).\r\n", creds.wifi_ssid);
    }

    // ── CYW43 / WiFi ─────────────────────────────────────────────────────────
    if (cyw43_arch_init()) {
        printf("FATAL: CYW43 init failed.\r\n");
        while (true) tight_loop_contents();
    }
    cyw43_arch_enable_sta_mode();
    // Enable CYW43 power-save mode: chip sleeps between beacon intervals
    // (~100 ms), significantly reducing idle WiFi draw while keeping the
    // MQTT connection and GPS subscription alive.
    cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM);

    if (!wifi_connect(creds.wifi_ssid, creds.wifi_pass)) {
        printf("Clearing credentials and rebooting for re-provisioning.\r\n");
        creds_invalidate();
        sleep_ms(500);
        watchdog_reboot(0, 0, 0);
        while (true);
    }

    // Give DHCP time to assign an IP before opening TCP connections.
    sleep_ms(1000);

    // ── BMP180 (I2C0, GP4/GP5) ───────────────────────────────────────────────
    struct bmp180_calib_param  params;
    struct bmp180_measurements measures;
    struct bmp180_model        sensor;

    global_i2c_init();
    bmp180_init(&sensor, &params, &measures);
    printf("BMP180 ready (chip ID: 0x%02X).\r\n", sensor.chipID);

    // ── BME280 (I2C0, GP4/GP5, addr 0x76) ───────────────────────────────────
    struct bme280_calib_param    bme_params;
    struct bme280_settings       bme_settings;
    struct bme280_measurements   bme_meas;
    struct bme280_model          bme_sensor;

    bme280_init(&bme_sensor, &bme_params, &bme_settings, &bme_meas);
    printf("BME280 ready (chip ID: 0x%02X).\r\n", bme_sensor.chipID);

    // ── INA219 (I2C1, GP6/GP7) ───────────────────────────────────────────────
    ina219_init();
    printf("INA219 ready.\r\n");

    // ── MQTT ─────────────────────────────────────────────────────────────────
    mqtt_ha_t mqtt = {0};
    // Start from the standard atmosphere so altitude is plausible before the
    // first GPS fix arrives. GPS calibration converges over ~20 readings (α=0.05).
    mqtt.qnh_ref_pa = 101325.0f;
    if (!mqtt_ha_connect(&mqtt, creds.mqtt_host, creds.mqtt_port)) {
        printf("Clearing credentials and rebooting for re-provisioning.\r\n");
        creds_invalidate();
        cyw43_arch_deinit();
        sleep_ms(500);
        watchdog_reboot(0, 0, 0);
        while (true);
    }

    // Publish HA auto-discovery then subscribe to GPS altitude topic.
    mqtt_ha_publish_discovery(&mqtt);
    mqtt_ha_subscribe_gps(&mqtt);
    mqtt_ha_publish_status(&mqtt, true);

    // ── Sensor loop ──────────────────────────────────────────────────────────
    while (true) {
        // Trigger BME280 forced-mode conversion immediately — it will finish
        // in ~40 ms and sleep automatically, ready to read after the BMP180
        // measurement and the 10 s interval sleep.
        bme_sensor.settings->mode = 0b01;
        bme280_start_measurements(&bme_sensor);

        bmp180_get_measurement(&sensor);

        float temp_c      = (float)sensor.measurement_params->T / 10.0f;
        float pressure_pa = (float)sensor.measurement_params->p;

        // Use the most recent GPS altitude for QNH normalisation.
        // Access inside lwip_begin/end because gps_altitude_m is written from
        // the MQTT incoming-data callback (IRQ context).
        cyw43_arch_lwip_begin();
        bool  gps_valid   = mqtt.gps_alt_valid;
        float station_alt = gps_valid ? mqtt.gps_altitude_m : FALLBACK_STATION_ALT_M;
        cyw43_arch_lwip_end();

        bmp180_compute_sea_pressure(&sensor, station_alt);
        float pressure_msl_pa = sensor.measurement_params->p_relative;

        // Update EMA QNH reference whenever GPS is providing real altitude.
        // α = 0.05 → equivalent to a ~20-sample running average (~3 min window).
        // qnh_ref_pa lives only in main context so no locking is needed.
        if (gps_valid) {
            mqtt.qnh_ref_pa = QNH_EMA_ALPHA * pressure_msl_pa + (1.0f - QNH_EMA_ALPHA) * mqtt.qnh_ref_pa;
        }

        // Barometric altitude from the calibrated QNH reference.
        bmp180_compute_altitude(&sensor, mqtt.qnh_ref_pa);
        float altitude_m = sensor.measurement_params->altitude;

        printf("BMP180: T=%5.1f C  P=%6.2f hPa  QNH=%6.2f hPa  Alt=%6.1f m %s\r\n",
               temp_c,
               pressure_pa     / 100.0f,
               pressure_msl_pa / 100.0f,
               altitude_m,
               gps_valid ? "" : "  [GPS fallback]");

        // ── BME280 ───────────────────────────────────────────────────────────
        // Conversion was triggered before the sleep — it finished ~40 ms in,
        // so this read is instant (no blocking poll needed).
        if (bme280_get_uncompensated_measurements(&bme_sensor) != BME280_OK) {
            printf("BME280: measurement read failed (still busy?)\r\n");
        }
        bme280_compensate_temp(&bme_sensor);
        bme280_compensate_press(&bme_sensor);
        bme280_compensate_hum(&bme_sensor);
        bme_sensor.settings->mode = 0b00;

        float bme_temp_c       = (float)bme_sensor.measure->T / 100.0f;
        float bme_press_pa     = (float)bme_sensor.measure->P / 256.0f;
        float bme_press_msl_pa = bme_press_pa / powf(1.0f - (station_alt / 44330.0f), 5.255f);
        float bme_altitude_m   = 44330.0f * (1.0f - powf(bme_press_pa / mqtt.qnh_ref_pa, 1.0f / 5.255f));
        float bme_hum_pct      = (float)bme_sensor.measure->H / 1024.0f;

        printf("BME280: T=%5.1f C  P=%6.2f hPa  QNH=%6.2f hPa  Alt=%6.1f m  H=%5.1f %%RH\r\n",
               bme_temp_c,
               bme_press_pa     / 100.0f,
               bme_press_msl_pa / 100.0f,
               bme_altitude_m,
               bme_hum_pct);

        // ── INA219 ───────────────────────────────────────────────────────────
        ina219_reading_t ups = {0};
        bool ups_ok = ina219_read(&ups);
        if (!ups_ok) {
            printf("INA219 read failed.\r\n");
        }

        printf("INA219: Bat=%3d%%  %.2fV  %.0fmA\r\n",
               ups.percent, ups.voltage_v, ups.current_ma);

        sensor_state_t state = {
            .bmp180_temp_c          = temp_c,
            .bmp180_pressure_pa     = pressure_pa,
            .bmp180_pressure_msl_pa = mqtt.qnh_ref_pa,
            .bmp180_altitude_m      = altitude_m,
            .bme280_temp_c          = bme_temp_c,
            .bme280_pressure_pa     = bme_press_pa,
            .bme280_pressure_msl_pa = bme_press_msl_pa,
            .bme280_altitude_m      = bme_altitude_m,
            .bme280_humidity_pct    = bme_hum_pct,
            .battery_pct         = ups_ok ? ups.percent    : -1,
            .voltage_v           = ups_ok ? ups.voltage_v  : 0.0f,
            .current_ma          = ups_ok ? ups.current_ma : 0.0f,
        };
        mqtt_ha_publish_state(&mqtt, &state);

        sleep_ms(MEASURE_INTERVAL_MS);
    }
}
