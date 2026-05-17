#include <stdio.h>
#include <math.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/clocks.h"
#include "hardware/pll.h"
#include "hardware/rtc.h"
#include "hardware/structs/scb.h"
#include "hardware/watchdog.h"
#include "hardware/i2c.h"

#include "bmp180.h"
#include "bme280.h"
#include "ina219.h"
#include "provisioning.h"
#include "mqtt_ha.h"

// Fallback station altitude used if no GPS fix has been received yet.
#define FALLBACK_STATION_ALT_M  1457.8f

// Minimum plausible GPS altitude for this site (m).
// GPS reports alt=0 when fix quality is too poor (2D-only fix or total loss).
// Any altitude below this floor is rejected so the Kalman filter is not
// poisoned — s_alt_est retains its last good estimate instead.
#define GPS_ALT_MIN_M           1400.0f

// WMO-aligned sampling: 1-min interval, publish every 10 samples (10 min).
// WiFi is only powered up on the 10th cycle, giving ~10% WiFi duty cycle.
#define MEASURE_INTERVAL_MS   60000
#define SAMPLES_PER_PUBLISH   10

// Tendency window: 19 entries × 10-min intervals = 180 min (3-hour WMO SYNOP).
#define TENDENCY_HISTORY      19

// EMA smoothing factor for QNH (sea-level pressure reference).
// α=0.20 → ~5-sample window (~50 min at 10-min publish rate).
#define QNH_EMA_ALPHA         0.2f

// Kalman filter parameters for GPS altitude (stationary sensor).
// R: GPS altitude measurement variance (m²). Data shows σ ≈ 11.2 m → R ≈ 125.
// Q: Process noise variance (m²). Near-zero because station altitude is fixed;
//    tiny value prevents the posterior variance from collapsing to exactly zero.
#define GPS_ALT_R  125.0f
#define GPS_ALT_Q  0.01f

// ---------------------------------------------------------------------------
// Static accumulators — survive SLEEPDEEP (SRAM retained when only PLL_SYS
// is stopped; no initialisation needed after first boot since .bss is zeroed)
// ---------------------------------------------------------------------------

static float         s_bmp_temp_sum  = 0.0f;
static float         s_bme_temp_sum  = 0.0f;
static float         s_bme_hum_sum   = 0.0f;
static float         s_volt_sum      = 0.0f;
static float         s_curr_sum      = 0.0f;
static float         s_batt_sum      = 0.0f;
static int           s_ups_ok_count  = 0;
static int           s_sample_count  = 0;
static press_hires_t s_hires[SAMPLES_PER_PUBLISH];

// 3-hour pressure tendency ring buffer (stores 10-min mean MSL in hPa)
static float         s_tend_buf[TENDENCY_HISTORY];
static int           s_tend_count    = 0;  // entries filled so far (caps at TENDENCY_HISTORY)
static int           s_tend_write    = 0;  // index of next write slot
// Hysteresis state for tendency direction (-1=falling, 0=steady, +1=rising).
// Reset alongside the ring whenever a publish cycle is skipped.
static int           s_tend_dir      = 0;
static int           s_last_tend_a   = 4;  // last reported WMO code; default Steady

// Kalman filter state for GPS altitude.
// Initialised from flash (alt_load) in main() before first use.
// Persists across deep sleep via SRAM retention; saved to flash on GPS update.
static float         s_alt_est;   // posterior altitude estimate (m)
static float         s_alt_var;   // posterior variance (m²)

// Last values actually written to flash — used to suppress redundant writes.
// Initialised from flash in main() alongside s_alt_est / qnh_ref_pa.
// Flash is only written when either value moves beyond its threshold:
//   altitude: > 0.5 m  (converged Kalman drift < 0.1 m/update → near-zero writes)
//   QNH:      > 10 Pa (= 0.1 hPa) to capture real weather changes
static float         s_saved_alt;
static float         s_saved_qnh;

// ---------------------------------------------------------------------------
// WMO hypsometric formula with virtual temperature correction (WMO SLP)
// ---------------------------------------------------------------------------
//
// ICAO simplified QNH = P_station / (1 - h/44330)^5.255
// That formula implicitly assumes ISA temperature at altitude h (≈5.5°C here).
// The WMO meteorological SLP standard uses the full hypsometric formula with
// virtual temperature Tv, which accounts for actual T and humidity.
//
// At 1458 m with T≈15°C the correction is ~-3.7 hPa vs ICAO QNH.
// Both raw (ICAO) and corrected (Tv) values are published so neither is lost.
//
// P_station_pa: station pressure (Pa)
// alt_m:        station altitude (m)
// T_c:          air temperature (°C) — use BME280 (more accurate than BMP180)
// RH:           relative humidity (%) — from BME280
// Returns:      sea-level pressure (Pa) via hypsometric+Tv
static float msl_virtual_temp(float P_station_pa, float alt_m,
                               float T_c, float RH) {
    float es_hpa = 6.1078f * expf(17.27f * T_c / (T_c + 237.3f));
    float e_hpa  = (RH / 100.0f) * es_hpa;
    float T_k    = T_c + 273.15f;
    float P_hpa  = P_station_pa / 100.0f;
    float Tv     = T_k / (1.0f - (e_hpa / P_hpa) * (1.0f - 0.622f));
    return P_station_pa * expf(9.80665f * alt_m / (287.058f * Tv));
}

// ---------------------------------------------------------------------------
// WMO pressure tendency characteristic (code table 0200)
// ---------------------------------------------------------------------------

// Net 3-hour change (hPa) required to leave "Steady" — WMO synoptic standard.
// Previously 0.5 hPa (twice as sensitive); data showed the code changing on
// 93% of 10-min publish cycles, confirming that 0.5 was below the noise floor.
#define TEND_SIG  1.0f
// Difference between halves required to call a rise/fall "accelerating" or
// "decelerating" rather than "steady".
#define TEND_ACC  0.3f
// Extra hPa beyond TEND_SIG required to *commit* a direction change.
// Within this deadband the current direction is held, preventing rapid
// Rising ↔ Falling oscillation when net hovers near ±TEND_SIG.
#define TEND_HYST 0.3f

// Derive the WMO `a` code from the current tendency ring buffer.
// Requires s_tend_count == TENDENCY_HISTORY (called only when tendency_valid).
//
// Logical layout of the 19-slot ring after the most recent write:
//   index  0 (oldest, 3h ago)  = s_tend_buf[s_tend_write]
//   index  9 (midpoint, 1.5h)  = s_tend_buf[(s_tend_write + 9)  % 19]
//   index 18 (newest, now)     = s_tend_buf[(s_tend_write + 18) % 19]
//                              = s_tend_buf[(s_tend_write - 1 + 19) % 19]
static int tendency_a_code(void) {
    float oldest = s_tend_buf[s_tend_write];
    float mid    = s_tend_buf[(s_tend_write + 9)  % TENDENCY_HISTORY];
    float newest = s_tend_buf[(s_tend_write - 1 + TENDENCY_HISTORY) % TENDENCY_HISTORY];

    float net = newest - oldest;  // total 3-hour change
    float f   = mid    - oldest;  // first 1.5h
    float s   = newest - mid;     // second 1.5h

    if (net > TEND_SIG) {        // net rising
        if (s < -TEND_SIG)           return 0; // rose then fell, net still +
        if (f < TEND_SIG)            return 3; // was steady/falling then rose
        if (s > f + TEND_ACC)        return 3; // accelerating rise
        if (f > s + TEND_ACC)        return 1; // decelerating rise
        return 2;                              // rising steadily
    }
    if (net < -TEND_SIG) {       // net falling
        if (s >  TEND_SIG)           return 5; // fell then rose, net still -
        if (f > -TEND_SIG)           return 8; // was steady/rising then fell
        if (-s > -f + TEND_ACC)      return 8; // accelerating fall
        if (-f > -s + TEND_ACC)      return 6; // decelerating fall
        return 7;                              // falling steadily
    }
    // net steady (within ±TEND_SIG)
    if (f >  TEND_SIG && s < -TEND_SIG) return 0; // rose then fell back
    if (f < -TEND_SIG && s >  TEND_SIG) return 5; // fell then rose back
    return 4;                                      // steady throughout
}

// Wraps tendency_a_code() with direction-level hysteresis.
//
// Problem: the midpoint sample (1.5h ago) is a single noisy 10-min mean.
// An outlier there flips `f` and `s`, making the code jump between e.g.
// "Rising rapidly" and "Falling rapidly" on consecutive 10-min cycles.
//
// Fix: track the committed direction (-1/0/+1) separately.  A direction
// change only commits when net crosses TEND_SIG ± TEND_HYST (deadband).
// Within the same direction, subcode updates (e.g. Rising→Rising rapidly)
// are always accepted so the WMO shape analysis still has meaning.
static int tendency_a_filtered(float tendency_hpa) {
    int raw = tendency_a_code();

    // Determine new direction from the continuous net change.
    int new_dir;
    if      (tendency_hpa >  TEND_SIG + TEND_HYST) new_dir =  1;  // confirmed rising
    else if (tendency_hpa < -(TEND_SIG + TEND_HYST)) new_dir = -1; // confirmed falling
    else if (fabsf(tendency_hpa) <= TEND_SIG - TEND_HYST) new_dir =  0; // confirmed steady
    else new_dir = s_tend_dir;  // in hysteresis band: hold current direction

    int out;
    if (new_dir == 0) {
        out = 4;  // Steady regardless of raw shape analysis
    } else if (new_dir == s_tend_dir && new_dir != 0) {
        // Continuing the same direction: accept a subcode update only if the
        // raw code agrees with the current direction (codes 0-3 = net rising,
        // codes 5-8 = net falling).  A disagreeing raw code means the ring
        // net briefly dipped below TEND_SIG — keep the last valid subcode.
        bool agrees = (new_dir > 0) ? (raw <= 3) : (raw >= 5);
        out = agrees ? raw : s_last_tend_a;
    } else {
        // Direction genuinely changed: commit the new raw code.
        out = raw;
    }

    s_tend_dir   = new_dir;
    s_last_tend_a = out;
    return out;
}

// ---------------------------------------------------------------------------
// RTC sleep helper
// ---------------------------------------------------------------------------

// Flag set by the RTC alarm IRQ. Volatile so the compiler does not cache it.
static volatile bool s_rtc_fired;

static void rtc_alarm_cb(void) { s_rtc_fired = true; }

static void datetime_add_seconds(datetime_t *dt, int32_t secs) {
    int32_t s = dt->sec + secs;
    dt->sec   = (int8_t)(s % 60);
    int32_t m = dt->min + s / 60;
    dt->min   = (int8_t)(m % 60);
    dt->hour  = (int8_t)((dt->hour + m / 60) % 24);
}

// Enter deep sleep until an RTC alarm fires MEASURE_INTERVAL_MS ms from now.
// Stops PLL_SYS only (PLL_USB left running so USB serial survives).
// Restores 125 MHz and re-inits I2C before returning.
static void rtc_deep_sleep(void) {
    datetime_t base = {
        .year=2020, .month=1, .day=1, .dotw=3,
        .hour=0, .min=0, .sec=0
    };
    rtc_set_datetime(&base);
    sleep_us(64);

    datetime_t alarm = base;
    datetime_add_seconds(&alarm, MEASURE_INTERVAL_MS / 1000);

    clock_configure(clk_ref, CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC,
                    0, 12 * MHZ, 12 * MHZ);
    clock_configure(clk_sys, CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLK_REF,
                    0, 12 * MHZ, 12 * MHZ);
    clock_configure(clk_peri, 0, CLOCKS_CLK_PERI_CTRL_AUXSRC_VALUE_CLK_SYS,
                    12 * MHZ, 12 * MHZ);
    pll_deinit(pll_sys);

    s_rtc_fired = false;
    rtc_set_alarm(&alarm, rtc_alarm_cb);
    uint32_t scr_save = scb_hw->scr;
    hw_set_bits(&scb_hw->scr, 1u << 2u);
    // Loop WFI: wfi wakes on ANY interrupt (e.g. USB SOF every 1 ms when USB
    // is connected). Re-enter sleep until the RTC alarm callback fires.
    while (!s_rtc_fired) {
        __asm volatile("wfi" ::: "memory");
    }
    scb_hw->scr = scr_save;
    rtc_disable_alarm();

    set_sys_clock_khz(125000, true);
    global_i2c_init();
}

// ---------------------------------------------------------------------------
// WiFi connect (single attempt, 30 s timeout)
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
// Reset all 10-min accumulators (called after publish or on skipped cycle)
// ---------------------------------------------------------------------------

static void reset_accumulators(void) {
    s_bmp_temp_sum = 0.0f;
    s_bme_temp_sum = 0.0f;
    s_bme_hum_sum  = 0.0f;
    s_volt_sum     = 0.0f;
    s_curr_sum     = 0.0f;
    s_batt_sum     = 0.0f;
    s_ups_ok_count = 0;
    s_sample_count = 0;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
    stdio_init_all();

    // ── Credentials ──────────────────────────────────────────────────────────
    creds_t creds;
    bool has_creds = creds_load(&creds);

    if (!has_creds) {
        absolute_time_t t = make_timeout_time_ms(30000);
        while (!stdio_usb_connected() && !time_reached(t)) sleep_ms(100);
        creds_provision(&creds);
    } else {
        absolute_time_t t = make_timeout_time_ms(3000);
        while (!stdio_usb_connected() && !time_reached(t)) sleep_ms(100);
        printf("Loaded credentials from flash (SSID: %s).\r\n", creds.wifi_ssid);
    }
    printf("\r\npico-w-env-sensor starting (1-min sampling / 10-min publish)...\r\n");

    // ── Sensors (initialised once, persist through all cycles) ───────────────
    struct bmp180_calib_param  bmp_params;
    struct bmp180_measurements bmp_meas_buf;
    struct bmp180_model        bmp_sensor;
    struct bme280_calib_param  bme_params;
    struct bme280_settings     bme_settings;
    struct bme280_measurements bme_meas_buf;
    struct bme280_model        bme_sensor;

    global_i2c_init();
    bmp180_init(&bmp_sensor, &bmp_params, &bmp_meas_buf);
    printf("BMP180 ready (chip ID: 0x%02X).\r\n", bmp_sensor.chipID);
    bme280_init(&bme_sensor, &bme_params, &bme_settings, &bme_meas_buf);
    printf("BME280 ready (chip ID: 0x%02X).\r\n", bme_sensor.chipID);
    ina219_init();
    printf("INA219 ready.\r\n");

    float qnh_ref_pa = qnh_load();
    alt_load(&s_alt_est, &s_alt_var);
    s_saved_alt = s_alt_est;   // seed change-detection from flash
    s_saved_qnh = qnh_ref_pa;
    printf("Kalman alt: est=%.1f m  var=%.1f m² (σ=%.1f m)\r\n",
           s_alt_est, s_alt_var, sqrtf(s_alt_var));
    rtc_init();

    // ── Boot WiFi (wipe credentials and reboot on failure) ───────────────────
    if (cyw43_arch_init()) {
        printf("FATAL: CYW43 init failed.\r\n");
        while (true) tight_loop_contents();
    }
    cyw43_arch_enable_sta_mode();
    cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM);

    if (!wifi_connect(creds.wifi_ssid, creds.wifi_pass)) {
        printf("Boot WiFi failed — clearing credentials and rebooting.\r\n");
        creds_invalidate();
        cyw43_arch_deinit();
        sleep_ms(500);
        watchdog_reboot(0, 0, 0);
        while (true);
    }
    sleep_ms(1000);

    // ── Boot MQTT (wipe credentials and reboot on failure) ───────────────────
    {
        mqtt_ha_t mqtt = {0};
        mqtt.qnh_ref_pa = qnh_ref_pa;

        if (!mqtt_ha_connect(&mqtt, creds.mqtt_host, creds.mqtt_port)) {
            printf("Boot MQTT failed — clearing credentials and rebooting.\r\n");
            creds_invalidate();
            cyw43_arch_deinit();
            sleep_ms(500);
            watchdog_reboot(0, 0, 0);
            while (true);
        }

        // expire_after = full 10-min publish interval + 30 s buffer
        uint32_t publish_interval_s = (uint32_t)SAMPLES_PER_PUBLISH *
                                      MEASURE_INTERVAL_MS / 1000;
        mqtt_ha_publish_discovery(&mqtt, publish_interval_s + 30);
        mqtt_ha_publish_status(&mqtt, true);

        // GPS fetch → Kalman altitude update.
        // Reject altitude below GPS_ALT_MIN_M (2D-only fix / fix-loss → alt≈0).
        float gps_alt;
        bool gps_ok = mqtt_ha_fetch_gps_altitude(&mqtt, &gps_alt);
        if (gps_ok && gps_alt >= GPS_ALT_MIN_M) {
            // 1D Kalman update: predict (add tiny process noise) then correct.
            s_alt_var += GPS_ALT_Q;
            float K    = s_alt_var / (s_alt_var + GPS_ALT_R);
            s_alt_est += K * (gps_alt - s_alt_est);
            s_alt_var  = (1.0f - K) * s_alt_var;
            printf("GPS alt %.1f m → Kalman: est=%.1f m  σ=%.1f m\r\n",
                   gps_alt, s_alt_est, sqrtf(s_alt_var));
        } else if (gps_ok) {
            printf("GPS alt %.1f m below minimum (%.0f m) — ignored, Kalman est=%.1f m\r\n",
                   gps_alt, GPS_ALT_MIN_M, s_alt_est);
            gps_ok = false;
        }

        // Single boot reading (not a 10-min average — cold-start snapshot)
        bme_sensor.settings->mode = 0b01;
        bme280_start_measurements(&bme_sensor);
        bmp180_get_measurement(&bmp_sensor);

        float bmp_temp_c   = (float)bmp_sensor.measurement_params->T / 10.0f;
        float bmp_press_pa = (float)bmp_sensor.measurement_params->p;
        bmp180_compute_sea_pressure(&bmp_sensor, s_alt_est);
        float bmp_press_msl_pa = bmp_sensor.measurement_params->p_relative;
        if (gps_ok) {
            qnh_ref_pa = QNH_EMA_ALPHA * bmp_press_msl_pa +
                         (1.0f - QNH_EMA_ALPHA) * qnh_ref_pa;
            if (fabsf(s_alt_est - s_saved_alt) > 0.5f ||
                fabsf(qnh_ref_pa - s_saved_qnh) > 10.0f) {
                params_save(qnh_ref_pa, s_alt_est, s_alt_var);
                s_saved_alt = s_alt_est;
                s_saved_qnh = qnh_ref_pa;
            }
        }
        bmp180_compute_altitude(&bmp_sensor, qnh_ref_pa);
        float bmp_alt_m = bmp_sensor.measurement_params->altitude;

        if (bme280_get_uncompensated_measurements(&bme_sensor) != BME280_OK)
            printf("BME280: measurement read failed\r\n");
        bme280_compensate_temp(&bme_sensor);
        bme280_compensate_press(&bme_sensor);
        bme280_compensate_hum(&bme_sensor);
        bme_sensor.settings->mode = 0b00;

        float bme_temp_c       = (float)bme_sensor.measure->T / 100.0f;
        float bme_press_pa     = (float)bme_sensor.measure->P / 256.0f;
        float bme_press_msl_pa = bme_press_pa /
                                 powf(1.0f - (s_alt_est / 44330.0f), 5.255f);
        float bme_alt_m        = 44330.0f *
                                 (1.0f - powf(bme_press_pa / qnh_ref_pa, 1.0f / 5.255f));
        float bme_hum_pct      = (float)bme_sensor.measure->H / 1024.0f;

        // WMO hypsometric+Tv corrected MSL.  Both sensors use BME280 T+RH
        // for the virtual temperature (BME280 has better T accuracy than BMP180).
        float bmp_press_msl_vt_pa = msl_virtual_temp(bmp_press_pa, s_alt_est,
                                                      bme_temp_c, bme_hum_pct);
        float bme_press_msl_vt_pa = msl_virtual_temp(bme_press_pa, s_alt_est,
                                                      bme_temp_c, bme_hum_pct);

        ina219_reading_t ups = {0};
        bool ups_ok = ina219_read(&ups);

        printf("BMP180: T=%5.1f C  P=%6.2f hPa  QNH=%6.2f hPa  Alt=%6.1f m%s\r\n",
               bmp_temp_c, bmp_press_pa / 100.0f,
               bmp_press_msl_pa / 100.0f, bmp_alt_m,
               gps_ok ? "" : "  [GPS fallback]");
        printf("BME280: T=%5.1f C  P=%6.2f hPa  QNH=%6.2f hPa  Alt=%6.1f m  H=%5.1f %%RH\r\n",
               bme_temp_c, bme_press_pa / 100.0f,
               bme_press_msl_pa / 100.0f, bme_alt_m, bme_hum_pct);
        printf("INA219: Bat=%3d%%  %.2fV  %.0fmA\r\n",
               ups_ok ? ups.percent : -1, ups.voltage_v, ups.current_ma);

        sensor_state_t state = {
            .bmp180_temp_c          = bmp_temp_c,
            .bmp180_pressure_pa     = bmp_press_pa,
            .bmp180_pressure_msl_pa = bmp_press_msl_vt_pa,  // WMO Tv-corrected
            .bmp180_altitude_m      = bmp_alt_m,
            .bme280_temp_c          = bme_temp_c,
            .bme280_pressure_pa     = bme_press_pa,
            .bme280_pressure_msl_pa = bme_press_msl_vt_pa,  // WMO Tv-corrected
            .bme280_altitude_m      = bme_alt_m,
            .bme280_humidity_pct    = bme_hum_pct,
            .battery_pct    = ups_ok ? ups.percent    : -1,
            .voltage_v      = ups_ok ? ups.voltage_v  : 0.0f,
            .current_ma     = ups_ok ? ups.current_ma : 0.0f,
            .tendency_a     = 0,
            .tendency_valid = false,   // no history on boot
            .station_alt_m  = s_alt_est,
        };
        mqtt_ha_publish_state(&mqtt, &state);
        sleep_ms(100);
        mqtt_ha_disconnect(&mqtt);
    }
    cyw43_arch_deinit();

    // ── Main loop: 1-min sample, WiFi+publish every SAMPLES_PER_PUBLISH ───────
    while (true) {
        // Deep sleep ~1 min (PLL_SYS off, SRAM retained, USB alive)
        rtc_deep_sleep();
        qnh_ref_pa = qnh_load();

        // ── 1-min sensor reading ──────────────────────────────────────────────
        bme_sensor.settings->mode = 0b01;
        bme280_start_measurements(&bme_sensor);
        bmp180_get_measurement(&bmp_sensor);

        float bmp_temp_c   = (float)bmp_sensor.measurement_params->T / 10.0f;
        float bmp_press_pa = (float)bmp_sensor.measurement_params->p;
        bmp180_compute_sea_pressure(&bmp_sensor, s_alt_est);
        float bmp_press_msl_pa = bmp_sensor.measurement_params->p_relative;

        if (bme280_get_uncompensated_measurements(&bme_sensor) != BME280_OK)
            printf("BME280: measurement read failed\r\n");
        bme280_compensate_temp(&bme_sensor);
        bme280_compensate_press(&bme_sensor);
        bme280_compensate_hum(&bme_sensor);
        bme_sensor.settings->mode = 0b00;

        float bme_temp_c       = (float)bme_sensor.measure->T / 100.0f;
        float bme_press_pa     = (float)bme_sensor.measure->P / 256.0f;
        float bme_press_msl_pa = bme_press_pa /
                                 powf(1.0f - (s_alt_est / 44330.0f), 5.255f);
        float bme_hum_pct      = (float)bme_sensor.measure->H / 1024.0f;

        ina219_reading_t ups = {0};
        bool ups_ok = ina219_read(&ups);

        printf("Sample %d/%d — BMP180: T=%.1f P=%.2fhPa | BME280: T=%.1f H=%.1f%%\r\n",
               s_sample_count + 1, SAMPLES_PER_PUBLISH,
               bmp_temp_c, bmp_press_pa / 100.0f, bme_temp_c, bme_hum_pct);

        // ── Accumulate ────────────────────────────────────────────────────────
        s_bmp_temp_sum += bmp_temp_c;
        s_bme_temp_sum += bme_temp_c;
        s_bme_hum_sum  += bme_hum_pct;
        if (ups_ok) {
            s_volt_sum += ups.voltage_v;
            s_curr_sum += ups.current_ma;
            s_batt_sum += (float)ups.percent;
            s_ups_ok_count++;
        }
        s_hires[s_sample_count] = (press_hires_t){
            .bmp_pa = bmp_press_pa / 100.0f,
            .bme_pa = bme_press_pa / 100.0f,
        };
        s_sample_count++;

        if (s_sample_count < SAMPLES_PER_PUBLISH) continue;

        // ── Every 10 samples (10 min): compute averages and publish ───────────
        float n = (float)SAMPLES_PER_PUBLISH;

        float bmp_temp_mean = s_bmp_temp_sum / n;
        float bme_temp_mean = s_bme_temp_sum / n;
        float bme_hum_mean  = s_bme_hum_sum  / n;

        // Most recent 1-min pressure sample (QFE) used for state topic.
        // Convert to MSL (QNH) using the current Kalman altitude estimate.
        // All 10 hires samples share the same altitude reference since GPS is
        // not yet fetched at this point in the cycle.
        float bmp_press_pa_last = s_hires[SAMPLES_PER_PUBLISH - 1].bmp_pa * 100.0f;
        float bme_press_pa_last = s_hires[SAMPLES_PER_PUBLISH - 1].bme_pa * 100.0f;

        float alt_factor            = powf(1.0f - (s_alt_est / 44330.0f), 5.255f);
        float bmp_press_msl_pa_last = bmp_press_pa_last / alt_factor;
        float bme_press_msl_pa_last = bme_press_pa_last / alt_factor;

        // 10-min mean MSL pressure for tendency (WMO: use averaged pressure).
        // BME280 is used — it has factory temperature compensation which gives
        // more accurate pressure readings than the BMP180.
        float bme_msl_mean_hpa = 0.0f;
        for (int i = 0; i < SAMPLES_PER_PUBLISH; i++)
            bme_msl_mean_hpa += s_hires[i].bme_pa;
        bme_msl_mean_hpa /= n;
        bme_msl_mean_hpa /= alt_factor;

        // Push 10-min mean into 3-hour tendency ring buffer
        s_tend_buf[s_tend_write] = bme_msl_mean_hpa;
        s_tend_write = (s_tend_write + 1) % TENDENCY_HISTORY;
        if (s_tend_count < TENDENCY_HISTORY) s_tend_count++;

        // Tendency = newest 10-min mean − oldest (3 h ago); needs full 19 entries
        bool  tendency_valid = (s_tend_count == TENDENCY_HISTORY);
        float tendency_hpa   = 0.0f;
        int   tendency_a     = 0;
        if (tendency_valid) {
            float newest = s_tend_buf[(s_tend_write - 1 + TENDENCY_HISTORY) % TENDENCY_HISTORY];
            float oldest = s_tend_buf[s_tend_write];
            tendency_hpa = newest - oldest;
            tendency_a   = tendency_a_filtered(tendency_hpa);
        }

        // INA219 10-min averages
        int   batt_mean = s_ups_ok_count > 0 ? (int)(s_batt_sum / s_ups_ok_count + 0.5f) : -1;
        float volt_mean = s_ups_ok_count > 0 ? s_volt_sum / s_ups_ok_count : 0.0f;
        float curr_mean = s_ups_ok_count > 0 ? s_curr_sum / s_ups_ok_count : 0.0f;

        // ── WiFi on ───────────────────────────────────────────────────────────
        if (cyw43_arch_init()) {
            printf("CYW43 init failed, skipping publish cycle.\r\n");
            // Invalidate tendency window — we've lost a slot in the timeline
            s_tend_count = 0;
            s_tend_write = 0;
            s_tend_dir   = 0;
            s_last_tend_a = 4;
            reset_accumulators();
            continue;
        }
        cyw43_arch_enable_sta_mode();
        cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM);

        while (!wifi_connect(creds.wifi_ssid, creds.wifi_pass)) {
            printf("WiFi retry in 10 s...\r\n");
            sleep_ms(10000);
        }
        sleep_ms(500);

        // ── MQTT connect (3 retries) ──────────────────────────────────────────
        mqtt_ha_t mqtt = {0};
        mqtt.qnh_ref_pa = qnh_ref_pa;
        bool mqtt_ok = false;
        for (int t = 0; t < 3 && !mqtt_ok; t++) {
            mqtt_ok = mqtt_ha_connect(&mqtt, creds.mqtt_host, creds.mqtt_port);
            if (!mqtt_ok) sleep_ms(5000);
        }
        if (!mqtt_ok) {
            printf("MQTT connect failed after 3 tries, skipping publish cycle.\r\n");
            cyw43_arch_deinit();
            s_tend_count = 0;
            s_tend_write = 0;
            s_tend_dir   = 0;
            s_last_tend_a = 4;
            reset_accumulators();
            continue;
        }
        mqtt_ha_publish_status(&mqtt, true);

        // ── GPS fetch → Kalman altitude update + QNH ─────────────────────────
        // Reject altitude below GPS_ALT_MIN_M (2D-only fix / fix-loss → alt≈0).
        float gps_alt;
        bool gps_ok = mqtt_ha_fetch_gps_altitude(&mqtt, &gps_alt);
        if (gps_ok && gps_alt < GPS_ALT_MIN_M) {
            printf("GPS alt %.1f m below minimum (%.0f m) — ignored, Kalman est=%.1f m\r\n",
                   gps_alt, GPS_ALT_MIN_M, s_alt_est);
            gps_ok = false;
        }
        if (gps_ok) {
            // 1D Kalman update: predict (add tiny process noise) then correct.
            s_alt_var += GPS_ALT_Q;
            float K    = s_alt_var / (s_alt_var + GPS_ALT_R);
            s_alt_est += K * (gps_alt - s_alt_est);
            s_alt_var  = (1.0f - K) * s_alt_var;
            printf("GPS alt %.1f m → Kalman: est=%.1f m  σ=%.1f m\r\n",
                   gps_alt, s_alt_est, sqrtf(s_alt_var));
            // Update QNH EMA using 10-min mean MSL pressure (more stable than single sample)
            qnh_ref_pa = QNH_EMA_ALPHA * (bme_msl_mean_hpa * 100.0f) +
                         (1.0f - QNH_EMA_ALPHA) * qnh_ref_pa;
            if (fabsf(s_alt_est - s_saved_alt) > 0.5f ||
                fabsf(qnh_ref_pa - s_saved_qnh) > 10.0f) {
                params_save(qnh_ref_pa, s_alt_est, s_alt_var);
                s_saved_alt = s_alt_est;
                s_saved_qnh = qnh_ref_pa;
            }
        }

        // Altitude from last pressure sample + current QNH
        float bmp_alt_m = 44330.0f *
                          (1.0f - powf(bmp_press_pa_last / qnh_ref_pa, 1.0f / 5.255f));
        float bme_alt_m = 44330.0f *
                          (1.0f - powf(bme_press_pa_last / qnh_ref_pa, 1.0f / 5.255f));

        // WMO hypsometric+Tv corrected MSL.  Use 10-min mean T and RH for
        // the virtual temperature (more stable than last-sample-only).
        // Both sensors use BME280 T+RH (better accuracy than BMP180 T).
        float bmp_press_msl_vt_pa = msl_virtual_temp(bmp_press_pa_last, s_alt_est,
                                                      bme_temp_mean, bme_hum_mean);
        float bme_press_msl_vt_pa = msl_virtual_temp(bme_press_pa_last, s_alt_est,
                                                      bme_temp_mean, bme_hum_mean);

        printf("Publish — BMP180 mean: T=%.1fC  QNH=%.2fhPa  Alt=%.1fm\r\n",
               bmp_temp_mean, bmp_press_msl_pa_last / 100.0f, bmp_alt_m);
        printf("          BME280 mean: T=%.1fC  H=%.1f%%  UPS: %d%% %.2fV %.0fmA\r\n",
               bme_temp_mean, bme_hum_mean, batt_mean, volt_mean, curr_mean);
        printf("          Kalman alt: %.1f m  σ=%.1f m%s\r\n",
               s_alt_est, sqrtf(s_alt_var), gps_ok ? "" : "  [GPS hold]");
        if (tendency_valid)
            printf("          Tendency (3h): %+.1f hPa/3h  a=%d\r\n",
                   tendency_hpa, tendency_a);
        else
            printf("          Tendency: accumulating (%d/%d)\r\n",
                   s_tend_count, TENDENCY_HISTORY);

        // ── Burst-publish 10 hires pressure readings ──────────────────────────
        mqtt_ha_publish_press_hires(&mqtt, s_hires, SAMPLES_PER_PUBLISH);

        // ── Publish 10-min averages + tendency to state topic ─────────────────
        sensor_state_t state = {
            .bmp180_temp_c          = bmp_temp_mean,
            .bmp180_pressure_pa     = bmp_press_pa_last,
            .bmp180_pressure_msl_pa = bmp_press_msl_vt_pa,  // WMO Tv-corrected
            .bmp180_altitude_m      = bmp_alt_m,
            .bme280_temp_c          = bme_temp_mean,
            .bme280_pressure_pa     = bme_press_pa_last,
            .bme280_pressure_msl_pa = bme_press_msl_vt_pa,  // WMO Tv-corrected
            .bme280_altitude_m      = bme_alt_m,
            .bme280_humidity_pct    = bme_hum_mean,
            .battery_pct            = batt_mean,
            .voltage_v              = volt_mean,
            .current_ma             = curr_mean,
            .bme280_tendency_hpa    = tendency_hpa,
            .tendency_a             = tendency_a,
            .tendency_valid         = tendency_valid,
            .station_alt_m          = s_alt_est,
        };
        mqtt_ha_publish_state(&mqtt, &state);
        sleep_ms(100);
        mqtt_ha_disconnect(&mqtt);
        cyw43_arch_deinit();

        // ── Reset accumulators for next 10-min window ─────────────────────────
        reset_accumulators();
    }
}
