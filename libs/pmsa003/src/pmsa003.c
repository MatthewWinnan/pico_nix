#include "pmsa003.h"

#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

void pmsa003_init(void) {
    uart_init(PMSA003_UART, PMSA003_BAUD);
    gpio_set_function(PMSA003_GP_TX, GPIO_FUNC_UART);
    gpio_set_function(PMSA003_GP_RX, GPIO_FUNC_UART);
    uart_set_format(PMSA003_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(PMSA003_UART, true);
}

bool pmsa003_read(pmsa003_data_t *out) {
    uint8_t buf[PMSA003_FRAME_LEN];

    // Sync: slide a 2-byte window looking for 0x42 0x4D within timeout.
    // The sensor transmits ~1 frame/s; a new frame will arrive within 2 s.
    uint32_t deadline = to_ms_since_boot(get_absolute_time()) + PMSA003_SYNC_TIMEOUT_MS;
    uint8_t prev = 0;
    bool synced = false;

    while (to_ms_since_boot(get_absolute_time()) < deadline) {
        // Wait up to 100 ms for the next byte (inter-byte gap at 9600 baud is ~1 ms)
        if (!uart_is_readable_within_us(PMSA003_UART, 100000)) continue;
        uint8_t b = uart_getc(PMSA003_UART);
        if (prev == PMSA003_START_BYTE_0 && b == PMSA003_START_BYTE_1) {
            synced = true;
            break;
        }
        prev = b;
    }

    if (!synced) return false;

    buf[0] = PMSA003_START_BYTE_0;
    buf[1] = PMSA003_START_BYTE_1;

    // Read the remaining 30 bytes (frame length + data + checksum).
    // At 9600 baud, 30 bytes arrive in ~31 ms; allow 200 ms per byte.
    for (int i = 2; i < PMSA003_FRAME_LEN; i++) {
        if (!uart_is_readable_within_us(PMSA003_UART, 200000)) return false;
        buf[i] = uart_getc(PMSA003_UART);
    }

    // Verify checksum: sum of bytes 0..29 must equal bytes 30-31 (big-endian).
    uint16_t sum = 0;
    for (int i = 0; i < 30; i++) sum += buf[i];
    uint16_t chk = ((uint16_t)buf[30] << 8) | buf[31];
    if (sum != chk) return false;

    // Atmospheric PM concentrations (bytes 10-15, big-endian uint16)
    out->pm10  = ((uint16_t)buf[10] << 8) | buf[11];
    out->pm25  = ((uint16_t)buf[12] << 8) | buf[13];
    out->pm100 = ((uint16_t)buf[14] << 8) | buf[15];

    // Particle counts per 0.1 L (bytes 16-27, big-endian uint16)
    out->cnt_03  = ((uint16_t)buf[16] << 8) | buf[17];
    out->cnt_05  = ((uint16_t)buf[18] << 8) | buf[19];
    out->cnt_10  = ((uint16_t)buf[20] << 8) | buf[21];
    out->cnt_25  = ((uint16_t)buf[22] << 8) | buf[23];
    out->cnt_50  = ((uint16_t)buf[24] << 8) | buf[25];
    out->cnt_100 = ((uint16_t)buf[26] << 8) | buf[27];

    return true;
}
