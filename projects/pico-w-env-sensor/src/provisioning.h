#ifndef PROVISIONING_H
#define PROVISIONING_H

#include <stdint.h>
#include <stdbool.h>

// Credentials are stored in the last 4 KB flash sector.
// The magic value 0xC0FFEE01 guards against uninitialised flash reads.

#define CREDS_MAGIC  0xC0FFEE01UL
#define QNH_MAGIC    0xC0FFEE02UL
#define ALT_MAGIC    0xC0FFEE03UL

typedef struct {
    uint32_t magic;
    char     wifi_ssid[64];
    char     wifi_pass[64];
    char     mqtt_host[128];   // IP address or hostname
    uint16_t mqtt_port;
    uint8_t  _pad[2];
    // QNH EMA reference (sea-level pressure, Pa).
    // Persisted across power cycles; guarded by its own magic so it
    // gracefully defaults to the standard atmosphere if never written.
    float    qnh_ref_pa;
    uint32_t qnh_magic;
    // Kalman filter state for GPS altitude (m).
    // alt_est: posterior altitude estimate; alt_var: posterior variance (m²).
    // Persisted so the filter survives power cycles without re-converging.
    float    alt_est;
    float    alt_var;
    uint32_t alt_magic;
} creds_t;

// Load credentials from flash into *out. Returns true if magic matches.
bool creds_load(creds_t *out);

// Prompt for credentials over USB serial (blocks until user presses Enter),
// then write to flash.
void creds_provision(creds_t *out);

// Erase the credential sector — clears the magic so next boot re-provisions.
// Also wipes the QNH EMA since both live in the same sector.
void creds_invalidate(void);

// Persist QNH EMA + Kalman altitude state in a single flash write.
// Use this after every GPS fetch to avoid two separate erase/program cycles.
// No-op if credentials have not been written.
void params_save(float qnh_pa, float alt_est, float alt_var);

// Load the persisted QNH EMA reference from flash.
// Returns 101325.0 (standard atmosphere) if it has never been written.
float qnh_load(void);

// Load the persisted Kalman altitude state from flash.
// Writes fallback defaults (1457.8 m / 125.0 m²) into *est/*var if never written.
void alt_load(float *est, float *var);

#endif // PROVISIONING_H
