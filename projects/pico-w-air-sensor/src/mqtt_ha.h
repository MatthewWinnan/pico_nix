#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <stdbool.h>
#include "lwip/apps/mqtt.h"

// MQTT topics
#define MQTT_STATE_TOPIC   "pico/air-sensor/state"
#define MQTT_STATUS_TOPIC  "pico/air-sensor/status"

// HA MQTT auto-discovery prefix
#define HA_DISC_PREFIX     "homeassistant"

typedef struct {
    mqtt_client_t *client;
    bool           connected;  // set by connection callback (IRQ)
} mqtt_ha_t;

// All sensor readings for a single 1-minute publish.
typedef struct {
    // 1-minute averages (always valid after first publish)
    uint16_t pm1_0, pm2_5, pm10;
    uint16_t cnt_03, cnt_05, cnt_10, cnt_25, cnt_50, cnt_100;
    // 1-hour rolling means — omitted from JSON until hourly_valid is true,
    // so HA shows "unknown" for the first hour.
    float pm2_5_1h;
    float pm10_1h;
    bool  hourly_valid;
    // EPA NowCast PM2.5 (weighted 12-hour mean) — valid once ≥2 hourly
    // snapshots exist (≥2 h uptime).  Omitted from JSON until nowcast_valid.
    float nowcast_pm2_5;
    bool  nowcast_valid;
    // WHO AQI category derived from NowCast PM2.5 — "Unknown" until valid.
    const char *aqi;  // "Good" / "Fair" / "Moderate" / "Poor" / "Very poor" / "Unknown"
} air_state_t;

// Initialise, connect, and wait until connected (or timeout).
// host may be a dotted-decimal IP or a DNS hostname.
// Returns false on failure.
bool mqtt_ha_connect(mqtt_ha_t *ctx, const char *host, uint16_t port);

// Publish HA MQTT auto-discovery configs for all air quality sensors.
// Call once after connect. Messages are retained.
void mqtt_ha_publish_discovery(mqtt_ha_t *ctx);

// Publish sensor state JSON to MQTT_STATE_TOPIC.
void mqtt_ha_publish_state(mqtt_ha_t *ctx, const air_state_t *state);

// Publish "online" or "offline" to MQTT_STATUS_TOPIC (retained).
void mqtt_ha_publish_status(mqtt_ha_t *ctx, bool online);

#endif // MQTT_HA_H
