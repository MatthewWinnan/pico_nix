#ifndef MQTT_HA_H
#define MQTT_HA_H

#include <stdbool.h>
#include "lwip/apps/mqtt.h"

// MQTT topics
#define MQTT_STATE_TOPIC       "pico/weather-aux/state"
#define MQTT_STATUS_TOPIC      "pico/weather-aux/status"
#define MQTT_GPS_TOPIC         "gps/fr3yr/tpv"
#define MQTT_PRESS_HIRES_TOPIC "pico/weather-aux/press_hires"

// HA MQTT auto-discovery prefix
#define HA_DISC_PREFIX     "homeassistant"

typedef struct {
    mqtt_client_t *client;
    bool           connected;      // set by connection callback (IRQ)
    float          gps_altitude_m; // updated by incoming GPS publish (IRQ)
    bool           gps_alt_valid;  // true once first GPS message received

    // Exponential moving average of GPS-derived QNH (sea-level pressure, Pa).
    // Updated in the main loop whenever a GPS fix is available.
    // Initialised to the standard atmosphere (101325 Pa) so altitude is
    // immediately plausible; GPS calibration converges over ~20 readings.
    float          qnh_ref_pa;
} mqtt_ha_t;

// One 1-minute pressure sample, stored in hPa.
// Ten of these are burst-published to MQTT_PRESS_HIRES_TOPIC every 10 min.
typedef struct {
    float bmp_pa;      // BMP180 station pressure (QFE), hPa
    float bmp_msl_pa;  // BMP180 sea-level pressure (QNH), hPa
    float bme_pa;      // BME280 station pressure (QFE), hPa
    float bme_msl_pa;  // BME280 sea-level pressure (QNH), hPa
} press_hires_t;

// Initialise, connect, and wait until connected (or timeout).
// host may be a dotted-decimal IP or a DNS hostname.
// Returns false on failure.
bool mqtt_ha_connect(mqtt_ha_t *ctx, const char *host, uint16_t port);

// Publish HA MQTT auto-discovery configs for all sensors.
// expire_after_s: seconds after which HA marks the entity unavailable.
// Set to (publish_interval_s + 30) — applies to all entities.
// Messages are retained so the broker replays them on HA restart.
void mqtt_ha_publish_discovery(mqtt_ha_t *ctx, uint32_t expire_after_s);

// Fetch the current GPS altitude from the broker's retained message.
// Subscribes to MQTT_GPS_TOPIC, waits up to 3 s for the retained message,
// then unsubscribes. Returns true and writes the altitude into *alt_m on
// success; returns false if no message arrives within the timeout.
bool mqtt_ha_fetch_gps_altitude(mqtt_ha_t *ctx, float *alt_m);

// Disconnect from MQTT and free the client.
void mqtt_ha_disconnect(mqtt_ha_t *ctx);

// All sensor readings bundled for a single 10-min publish.
typedef struct {
    // BMP180 — temperature is 10-min mean; pressure/altitude are last 1-min sample
    float bmp180_temp_c;
    float bmp180_pressure_pa;      // station pressure (QFE), Pa
    float bmp180_pressure_msl_pa;  // sea-level pressure (QNH), Pa
    float bmp180_altitude_m;
    // BME280 — temperature and humidity are 10-min means; pressure is last 1-min sample
    float bme280_temp_c;
    float bme280_pressure_pa;
    float bme280_pressure_msl_pa;
    float bme280_altitude_m;
    float bme280_humidity_pct;
    // INA219 (Pico-UPS-A) — all are 10-min means
    int   battery_pct;
    float voltage_v;
    float current_ma;
    // 3-hour pressure tendency — only present in JSON when tendency_valid is true
    float bme280_tendency_hpa;  // net hPa change over 3 h (BME280 MSL, more accurate)
    int   tendency_a;           // WMO characteristic code 0-8 (code table 0200)
    bool  tendency_valid;
} sensor_state_t;

// Publish sensor state JSON to MQTT_STATE_TOPIC.
// Pressures are converted from Pa to hPa in the payload.
// Tendency field is omitted from JSON when tendency_valid is false.
void mqtt_ha_publish_state(mqtt_ha_t *ctx, const sensor_state_t *state);

// Burst-publish count 1-minute pressure hires readings to MQTT_PRESS_HIRES_TOPIC.
// Each reading is published as a separate JSON message (not retained) with a
// 20 ms gap to avoid flooding the MQTT send queue.
void mqtt_ha_publish_press_hires(mqtt_ha_t *ctx,
                                 const press_hires_t *readings, int count);

// Publish "online" or "offline" to MQTT_STATUS_TOPIC (retained).
void mqtt_ha_publish_status(mqtt_ha_t *ctx, bool online);

#endif // MQTT_HA_H
