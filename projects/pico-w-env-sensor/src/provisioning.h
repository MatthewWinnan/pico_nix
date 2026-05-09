#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdint.h>
#include <stdbool.h>

// Credentials are stored in the last 4 KB flash sector.
// The magic value 0xC0FFEE01 guards against uninitialised flash reads.

#define CREDS_MAGIC  0xC0FFEE01UL

typedef struct {
    uint32_t magic;
    char     wifi_ssid[64];
    char     wifi_pass[64];
    char     mqtt_host[128];   // IP address or hostname
    uint16_t mqtt_port;
    uint8_t  _pad[2];
} creds_t;

// Load credentials from flash into *out. Returns true if magic matches.
bool creds_load(creds_t *out);

// Prompt for credentials over USB serial (blocks until user presses Enter),
// then write to flash.
void creds_provision(creds_t *out);

// Erase the credential sector — clears the magic so next boot re-provisions.
void creds_invalidate(void);

#endif // PROVISIONING_H
