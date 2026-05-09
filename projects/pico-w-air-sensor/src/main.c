#include <stdio.h>
#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "hardware/watchdog.h"
#include "hardware/structs/usb.h"
#include "pmsa003.h"
#include "ssd1306.h"
#include "provisioning.h"
#include "mqtt_ha.h"

// Number of sensor reads to hold each display page before rotating.
// The PMSA003 outputs ~1 frame/s, so each page shows for ~5 seconds.
#define READS_PER_PAGE    5
#define NUM_PAGES         3

// Average this many reads then publish (~10 seconds per publish).
#define PUBLISH_INTERVAL  10

// ---------------------------------------------------------------------------
// Display helpers — no-op when USB is not connected (field/battery mode).
//
// USB VBUS presence is read from the RP2040 USB hardware register at startup.
// This is instantaneous and requires no waiting or USB stack initialisation.
// When powered only from battery the bit is clear and the OLED is never used,
// saving both power and startup time.
// ---------------------------------------------------------------------------

static bool s_display;  // set once in main(); never changes after that

static void disp_init(void)                              { if (s_display) ssd1306_init(); }
static void disp_clear(void)                             { if (s_display) ssd1306_clear(); }
static void disp_show(void)                              { if (s_display) ssd1306_show(); }
static void disp_puts(int col, int row, const char *s)   { if (s_display) ssd1306_puts(col, row, s); }

// ---------------------------------------------------------------------------
// WiFi connect helper
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
// Display page renderers
// Each page is 4 rows × 16 columns of 8×8 px characters.
// Format: 6-char left label + 10-char right-justified value field = 16 chars.
// ---------------------------------------------------------------------------

static void draw_pm_page(const pmsa003_data_t *d) {
    char line[17];
    disp_clear();
    snprintf(line, sizeof(line), "PM1.0:%10u", d->pm10);
    disp_puts(0, 0, line);
    snprintf(line, sizeof(line), "PM2.5:%10u", d->pm25);
    disp_puts(0, 1, line);
    snprintf(line, sizeof(line), "PM10 :%10u", d->pm100);
    disp_puts(0, 2, line);
    disp_puts(0, 3, "         UG/M3  ");
    disp_show();
}

static void draw_count_small_page(const pmsa003_data_t *d) {
    char line[17];
    disp_clear();
    snprintf(line, sizeof(line), ">0.3U:%10u", d->cnt_03);
    disp_puts(0, 0, line);
    snprintf(line, sizeof(line), ">0.5U:%10u", d->cnt_05);
    disp_puts(0, 1, line);
    snprintf(line, sizeof(line), ">1.0U:%10u", d->cnt_10);
    disp_puts(0, 2, line);
    disp_puts(0, 3, "    CNT/0.1L    ");
    disp_show();
}

static void draw_count_large_page(const pmsa003_data_t *d) {
    char line[17];
    disp_clear();
    snprintf(line, sizeof(line), ">2.5U:%10u", d->cnt_25);
    disp_puts(0, 0, line);
    snprintf(line, sizeof(line), ">5.0U:%10u", d->cnt_50);
    disp_puts(0, 1, line);
    snprintf(line, sizeof(line), ">10 U:%10u", d->cnt_100);
    disp_puts(0, 2, line);
    disp_puts(0, 3, "    CNT/0.1L    ");
    disp_show();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main(void) {
    stdio_init_all();

    // Detect USB VBUS via the RP2040 USB hardware register — instantaneous,
    // no stack or waiting needed. Clear = battery-only field mode (no display).
    s_display = !!(usb_hw->sie_status & USB_SIE_STATUS_VBUS_DETECTED_BITS);

    disp_init();
    disp_clear();
    disp_puts(0, 0, "PICO AIR SENSOR ");
    disp_puts(0, 2, " INITIALISING.. ");
    disp_show();

    // ── Credentials ──────────────────────────────────────────────────────────
    creds_t creds;
    if (!creds_load(&creds)) {
        // No credentials in flash — wait for USB and prompt.
        disp_clear();
        disp_puts(0, 1, " CONNECT USB    ");
        disp_puts(0, 2, " TO PROVISION   ");
        disp_show();
        while (!stdio_usb_connected()) sleep_ms(100);
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
    cyw43_wifi_pm(&cyw43_state, CYW43_DEFAULT_PM);

    disp_clear();
    disp_puts(0, 1, "CONNECTING WIFI ");
    disp_show();

    if (!wifi_connect(creds.wifi_ssid, creds.wifi_pass)) {
        disp_clear();
        disp_puts(0, 1, " WIFI FAILED    ");
        disp_puts(0, 2, " REPROVISIONING ");
        disp_show();
        printf("Clearing credentials and rebooting for re-provisioning.\r\n");
        creds_invalidate();
        sleep_ms(2000);
        watchdog_reboot(0, 0, 0);
        while (true);
    }

    sleep_ms(1000);  // Give DHCP time to assign an IP

    // ── MQTT ─────────────────────────────────────────────────────────────────
    disp_clear();
    disp_puts(0, 1, "CONNECTING MQTT ");
    disp_show();

    mqtt_ha_t mqtt = {0};
    if (!mqtt_ha_connect(&mqtt, creds.mqtt_host, creds.mqtt_port)) {
        disp_clear();
        disp_puts(0, 1, " MQTT FAILED    ");
        disp_puts(0, 2, " REPROVISIONING ");
        disp_show();
        printf("Clearing credentials and rebooting for re-provisioning.\r\n");
        creds_invalidate();
        cyw43_arch_deinit();
        sleep_ms(2000);
        watchdog_reboot(0, 0, 0);
        while (true);
    }

    mqtt_ha_publish_discovery(&mqtt);
    mqtt_ha_publish_status(&mqtt, true);

    // ── PMSA003 ──────────────────────────────────────────────────────────────
    disp_clear();
    disp_puts(0, 1, "SENSOR WARMUP.. ");
    disp_show();

    pmsa003_init();
    sleep_ms(1000);  // let the sensor settle after UART init

    // ── Sensor loop ──────────────────────────────────────────────────────────
    uint32_t read_count = 0;

    // Accumulators for the rolling average over PUBLISH_INTERVAL reads.
    uint32_t acc_pm10 = 0, acc_pm25 = 0, acc_pm100 = 0;
    uint32_t acc_cnt_03 = 0, acc_cnt_05 = 0, acc_cnt_10 = 0;
    uint32_t acc_cnt_25 = 0, acc_cnt_50 = 0, acc_cnt_100 = 0;

    for (;;) {
        pmsa003_data_t data;

        if (!pmsa003_read(&data)) {
            disp_clear();
            disp_puts(1, 1, "  SENSOR ERROR  ");
            disp_show();
            printf("pmsa003_read timeout/checksum error\r\n");
            sleep_ms(1000);
            continue;
        }

        printf("PM1.0=%u PM2.5=%u PM10=%u ug/m3  "
               ">0.3=%u >0.5=%u >1.0=%u >2.5=%u >5.0=%u >10=%u /0.1L\r\n",
               data.pm10, data.pm25, data.pm100,
               data.cnt_03, data.cnt_05, data.cnt_10,
               data.cnt_25, data.cnt_50, data.cnt_100);

        acc_pm10    += data.pm10;
        acc_pm25    += data.pm25;
        acc_pm100   += data.pm100;
        acc_cnt_03  += data.cnt_03;
        acc_cnt_05  += data.cnt_05;
        acc_cnt_10  += data.cnt_10;
        acc_cnt_25  += data.cnt_25;
        acc_cnt_50  += data.cnt_50;
        acc_cnt_100 += data.cnt_100;

        read_count++;

        if (read_count % PUBLISH_INTERVAL == 0) {
            pmsa003_data_t avg = {
                .pm10    = (uint16_t)(acc_pm10    / PUBLISH_INTERVAL),
                .pm25    = (uint16_t)(acc_pm25    / PUBLISH_INTERVAL),
                .pm100   = (uint16_t)(acc_pm100   / PUBLISH_INTERVAL),
                .cnt_03  = (uint16_t)(acc_cnt_03  / PUBLISH_INTERVAL),
                .cnt_05  = (uint16_t)(acc_cnt_05  / PUBLISH_INTERVAL),
                .cnt_10  = (uint16_t)(acc_cnt_10  / PUBLISH_INTERVAL),
                .cnt_25  = (uint16_t)(acc_cnt_25  / PUBLISH_INTERVAL),
                .cnt_50  = (uint16_t)(acc_cnt_50  / PUBLISH_INTERVAL),
                .cnt_100 = (uint16_t)(acc_cnt_100 / PUBLISH_INTERVAL),
            };
            mqtt_ha_publish_state(&mqtt, &avg);

            acc_pm10 = acc_pm25 = acc_pm100 = 0;
            acc_cnt_03 = acc_cnt_05 = acc_cnt_10 = 0;
            acc_cnt_25 = acc_cnt_50 = acc_cnt_100 = 0;
        }

        uint32_t page = ((read_count - 1) / READS_PER_PAGE) % NUM_PAGES;
        switch (page) {
            case 0: draw_pm_page(&data);          break;
            case 1: draw_count_small_page(&data); break;
            case 2: draw_count_large_page(&data); break;
        }
    }
}
