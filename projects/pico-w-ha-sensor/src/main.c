#include <stdio.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"

#include "bmp180.h"
#include "ina219.h"
#include "provisioning.h"
#include "mqtt_ha.h"

// Fallback station altitude used if no GPS fix has been received yet.
// Set to the approximate elevation of your location (metres above sea level).
#define FALLBACK_STATION_ALT_M  1457.8f

// Sensor measurement interval in milliseconds
#define MEASURE_INTERVAL_MS  10000

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
            const float alpha = 0.05f;
            mqtt.qnh_ref_pa = alpha * pressure_msl_pa + (1.0f - alpha) * mqtt.qnh_ref_pa;
        }

        // Barometric altitude from the calibrated QNH reference.
        bmp180_compute_altitude(&sensor, mqtt.qnh_ref_pa);
        float altitude_m = sensor.measurement_params->altitude;

        // ── INA219 ───────────────────────────────────────────────────────────
        ina219_reading_t ups = {0};
        bool ups_ok = ina219_read(&ups);
        if (!ups_ok) {
            printf("INA219 read failed.\r\n");
        }

        printf("T=%5.1f C  P=%6.2f hPa  QNH=%6.2f hPa  Alt=%6.1f m  "
               "Bat=%3d%%  %.2fV  %.0fmA%s\r\n",
               temp_c,
               pressure_pa     / 100.0f,
               pressure_msl_pa / 100.0f,
               altitude_m,
               ups.percent, ups.voltage_v, ups.current_ma,
               gps_valid ? "" : "  [GPS fallback]");

        sensor_state_t state = {
            .temp_c         = temp_c,
            .pressure_pa    = pressure_pa,
            .pressure_msl_pa = pressure_msl_pa,
            .altitude_m     = altitude_m,
            .battery_pct    = ups_ok ? ups.percent    : -1,
            .voltage_v      = ups_ok ? ups.voltage_v  : 0.0f,
            .current_ma     = ups_ok ? ups.current_ma : 0.0f,
        };
        mqtt_ha_publish_state(&mqtt, &state);

        sleep_ms(MEASURE_INTERVAL_MS);
    }
}
