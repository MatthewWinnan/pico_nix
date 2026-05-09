#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <stdbool.h>
#include "lwip/apps/mqtt.h"
#include "pmsa003.h"

// MQTT topics
#define MQTT_STATE_TOPIC   "pico/air-sensor/state"
#define MQTT_STATUS_TOPIC  "pico/air-sensor/status"

// HA MQTT auto-discovery prefix
#define HA_DISC_PREFIX     "homeassistant"

typedef struct {
    mqtt_client_t *client;
    bool           connected;  // set by connection callback (IRQ)
} mqtt_ha_t;

// Initialise, connect, and wait until connected (or timeout).
// host may be a dotted-decimal IP or a DNS hostname.
// Returns false on failure.
bool mqtt_ha_connect(mqtt_ha_t *ctx, const char *host, uint16_t port);

// Publish HA MQTT auto-discovery configs for all air quality sensors.
// Call once after connect. Messages are retained.
void mqtt_ha_publish_discovery(mqtt_ha_t *ctx);

// Publish sensor state JSON to MQTT_STATE_TOPIC.
void mqtt_ha_publish_state(mqtt_ha_t *ctx, const pmsa003_data_t *data);

// Publish "online" or "offline" to MQTT_STATUS_TOPIC (retained).
void mqtt_ha_publish_status(mqtt_ha_t *ctx, bool online);

#endif // MQTT_HA_H
