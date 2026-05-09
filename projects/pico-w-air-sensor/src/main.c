#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pmsa003.h"
#include "ssd1306.h"

// Number of sensor reads to hold each display page before rotating.
// The PMSA003 outputs ~1 frame/s, so each page shows for ~5 seconds.
#define READS_PER_PAGE  5
#define NUM_PAGES       3

// ---------------------------------------------------------------------------
// Display page renderers
// Each page is 4 rows × 16 columns of 8×8 px characters.
// Format: 6-char left label + 10-char right-justified value field = 16 chars.
// ---------------------------------------------------------------------------

static void draw_pm_page(const pmsa003_data_t *d) {
    char line[17];
    ssd1306_clear();
    snprintf(line, sizeof(line), "PM1.0:%10u", d->pm10);
    ssd1306_puts(0, 0, line);
    snprintf(line, sizeof(line), "PM2.5:%10u", d->pm25);
    ssd1306_puts(0, 1, line);
    snprintf(line, sizeof(line), "PM10 :%10u", d->pm100);
    ssd1306_puts(0, 2, line);
    ssd1306_puts(0, 3, "         UG/M3  ");
    ssd1306_show();
}

static void draw_count_small_page(const pmsa003_data_t *d) {
    char line[17];
    ssd1306_clear();
    snprintf(line, sizeof(line), ">0.3U:%10u", d->cnt_03);
    ssd1306_puts(0, 0, line);
    snprintf(line, sizeof(line), ">0.5U:%10u", d->cnt_05);
    ssd1306_puts(0, 1, line);
    snprintf(line, sizeof(line), ">1.0U:%10u", d->cnt_10);
    ssd1306_puts(0, 2, line);
    ssd1306_puts(0, 3, "    CNT/0.1L    ");
    ssd1306_show();
}

static void draw_count_large_page(const pmsa003_data_t *d) {
    char line[17];
    ssd1306_clear();
    snprintf(line, sizeof(line), ">2.5U:%10u", d->cnt_25);
    ssd1306_puts(0, 0, line);
    snprintf(line, sizeof(line), ">5.0U:%10u", d->cnt_50);
    ssd1306_puts(0, 1, line);
    snprintf(line, sizeof(line), ">10 U:%10u", d->cnt_100);
    ssd1306_puts(0, 2, line);
    ssd1306_puts(0, 3, "    CNT/0.1L    ");
    ssd1306_show();
}

int main(void) {
    stdio_init_all();

    ssd1306_init();

    // Splash while the PMSA003 warms up
    ssd1306_clear();
    ssd1306_puts(0, 0, "PICO AIR MONITOR");
    ssd1306_puts(2, 2, "INITIALISING...");
    ssd1306_show();

    pmsa003_init();
    sleep_ms(1000);  // let the sensor settle after UART init

    uint32_t read_count = 0;

    for (;;) {
        pmsa003_data_t data;

        if (!pmsa003_read(&data)) {
            ssd1306_clear();
            ssd1306_puts(1, 1, "  SENSOR ERROR  ");
            ssd1306_show();
            printf("pmsa003_read timeout/checksum error\r\n");
            sleep_ms(1000);
            continue;
        }

        printf("PM1.0=%u PM2.5=%u PM10=%u ug/m3  "
               ">0.3=%u >0.5=%u >1.0=%u >2.5=%u >5.0=%u >10=%u /0.1L\r\n",
               data.pm10, data.pm25, data.pm100,
               data.cnt_03, data.cnt_05, data.cnt_10,
               data.cnt_25, data.cnt_50, data.cnt_100);

        uint32_t page = (read_count / READS_PER_PAGE) % NUM_PAGES;
        switch (page) {
            case 0: draw_pm_page(&data);          break;
            case 1: draw_count_small_page(&data); break;
            case 2: draw_count_large_page(&data); break;
        }

        read_count++;
    }
}
