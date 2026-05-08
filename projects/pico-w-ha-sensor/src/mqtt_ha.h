#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <stdbool.h>
#include "lwip/apps/mqtt.h"

// MQTT topics
#define MQTT_STATE_TOPIC   "pico/bmp180/state"
#define MQTT_STATUS_TOPIC  "pico/bmp180/status"
#define MQTT_GPS_TOPIC     "gps/fr3yr/tpv"

// HA MQTT auto-discovery prefix
#define HA_DISC_PREFIX     "homeassistant"

typedef struct {
    mqtt_client_t *client;
    bool           connected;      // set by connection callback (IRQ)
    float          gps_altitude_m; // updated by incoming GPS publish (IRQ)
    bool           gps_alt_valid;  // true once first GPS message received

    // Exponential moving average of GPS-derived QNH (sea-level pressure, Pa).
    // Updated in the main loop whenever a GPS fix is available.
    // Used as the reference pressure for barometric altitude computation.
    // Initialised to the standard atmosphere (101325 Pa) so altitude is
    // immediately plausible; GPS calibration converges over ~20 readings.
    float          qnh_ref_pa;
} mqtt_ha_t;

// Initialise, connect, and wait until connected (or timeout).
// host may be a dotted-decimal IP or a DNS hostname.
// Returns false on failure.
bool mqtt_ha_connect(mqtt_ha_t *ctx, const char *host, uint16_t port);

// Publish HA MQTT auto-discovery configs for all three sensors.
// Call once after connect. Messages are retained.
void mqtt_ha_publish_discovery(mqtt_ha_t *ctx);

// Subscribe to the GPS altitude topic and install incoming-data callbacks.
void mqtt_ha_subscribe_gps(mqtt_ha_t *ctx);

// All sensor readings bundled into one struct.
// Add fields here as new sensors are integrated — the publish function
// serialises the whole struct to a single JSON payload.
typedef struct {
    // BMP180
    float temp_c;
    float pressure_pa;      // station pressure (QFE), Pa
    float pressure_msl_pa;  // sea-level pressure (QNH), Pa
    float altitude_m;       // EMA-calibrated barometric altitude, m
    // INA219 (Pico-UPS-A)
    int   battery_pct;      // estimated remaining capacity, %
    float voltage_v;        // battery bus voltage, V
    float current_ma;       // charge (+) / discharge (-) current, mA
} sensor_state_t;

// Publish sensor state JSON to MQTT_STATE_TOPIC.
// Pressures are converted from Pa to hPa in the payload.
void mqtt_ha_publish_state(mqtt_ha_t *ctx, const sensor_state_t *state);

// Publish "online" or "offline" to MQTT_STATUS_TOPIC (retained).
void mqtt_ha_publish_status(mqtt_ha_t *ctx, bool online);

#endif // MQTT_HA_H
