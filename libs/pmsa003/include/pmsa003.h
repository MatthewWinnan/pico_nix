#pragma once
#include <stdbool.h>
#include <stdint.h>

#define PMSA003_UART          uart0
#define PMSA003_BAUD          9600
#define PMSA003_GP_TX         0
#define PMSA003_GP_RX         1

#define PMSA003_FRAME_LEN     32
#define PMSA003_START_BYTE_0  0x42
#define PMSA003_START_BYTE_1  0x4D
#define PMSA003_SYNC_TIMEOUT_MS 2000

typedef struct {
    uint16_t pm10;    // PM1.0 concentration (μg/m³, atmospheric)
    uint16_t pm25;    // PM2.5 concentration (μg/m³, atmospheric)
    uint16_t pm100;   // PM10  concentration (μg/m³, atmospheric)
    uint16_t cnt_03;  // Particle count >0.3 μm per 0.1 L
    uint16_t cnt_05;  // Particle count >0.5 μm per 0.1 L
    uint16_t cnt_10;  // Particle count >1.0 μm per 0.1 L
    uint16_t cnt_25;  // Particle count >2.5 μm per 0.1 L
    uint16_t cnt_50;  // Particle count >5.0 μm per 0.1 L
    uint16_t cnt_100; // Particle count >10  μm per 0.1 L
} pmsa003_data_t;

// Initialise UART0 on GP0 (TX) / GP1 (RX) at 9600 baud 8N1.
void pmsa003_init(void);

// Block until a valid 32-byte frame is received (≤2 s typical).
// Returns true on success, false on timeout or checksum error.
bool pmsa003_read(pmsa003_data_t *out);
