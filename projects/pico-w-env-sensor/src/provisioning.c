#include "provisioning.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "hardware/flash.h"
#include "hardware/sync.h"

// Reserve the last 4 KB sector of the 2 MB flash for credential storage.
// PICO_FLASH_SIZE_BYTES is defined by the SDK for the target board.
#define CREDS_FLASH_OFFSET  (PICO_FLASH_SIZE_BYTES - FLASH_SECTOR_SIZE)
// XIP_BASE maps flash into the CPU address space for read-only access.
#define CREDS_FLASH_ADDR    (XIP_BASE + CREDS_FLASH_OFFSET)

bool creds_load(creds_t *out) {
    const creds_t *stored = (const creds_t *)CREDS_FLASH_ADDR;
    if (stored->magic != CREDS_MAGIC) return false;
    memcpy(out, stored, sizeof(creds_t));
    return true;
}

void creds_invalidate(void) {
    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(CREDS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    restore_interrupts(irq);
}

// Number of 256-byte flash pages needed to hold creds_t.
// sizeof(creds_t) == 264, so this evaluates to 2 at compile time.
#define CREDS_PAGES  ((sizeof(creds_t) + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE)

// Write *in to flash (erase sector first).
// Called with interrupts already safe to disable (not in IRQ).
static void creds_save(const creds_t *in) {
    // flash_range_program length must be a multiple of FLASH_PAGE_SIZE (256 B).
    // sizeof(creds_t) = 264 B > 256 B, so we need 2 pages (512 B).
    uint8_t buf[CREDS_PAGES * FLASH_PAGE_SIZE];
    memset(buf, 0xff, sizeof(buf));   // unprogrammed flash reads as 0xff
    memcpy(buf, in, sizeof(creds_t));

    uint32_t irq = save_and_disable_interrupts();
    flash_range_erase(CREDS_FLASH_OFFSET, FLASH_SECTOR_SIZE);
    flash_range_program(CREDS_FLASH_OFFSET, buf, sizeof(buf));
    restore_interrupts(irq);
}

// Read one line from USB serial with local echo and backspace support.
static void read_line(char *buf, size_t maxlen) {
    size_t i = 0;
    while (i < maxlen - 1) {
        int c = getchar();
        if (c == '\r' || c == '\n') {
            if (i > 0) break;  // end of line
            continue;           // skip leading CR/LF (e.g. stray \n after CRLF)
        }
        if ((c == '\b' || c == 0x7f) && i > 0) {
            i--;
            putchar('\b');
            putchar(' ');
            putchar('\b');
            continue;
        }
        buf[i++] = (char)c;
        putchar(c);  // echo
    }
    buf[i] = '\0';
    printf("\r\n");
}

void creds_provision(creds_t *out) {
    memset(out, 0, sizeof(creds_t));

    printf("\r\n=== WiFi / MQTT Provisioning ===\r\n");
    printf("Enter credentials. Press Enter after each field.\r\n\r\n");

    printf("WiFi SSID     : ");
    read_line(out->wifi_ssid, sizeof(out->wifi_ssid));

    printf("WiFi Password : ");
    read_line(out->wifi_pass, sizeof(out->wifi_pass));

    printf("MQTT Host (IP or hostname): ");
    read_line(out->mqtt_host, sizeof(out->mqtt_host));

    char port_buf[8] = {0};
    printf("MQTT Port [1883]: ");
    read_line(port_buf, sizeof(port_buf));
    out->mqtt_port = (port_buf[0] != '\0') ? (uint16_t)atoi(port_buf) : 1883;

    out->magic = CREDS_MAGIC;
    creds_save(out);

    printf("Credentials saved to flash.\r\n\r\n");
}
