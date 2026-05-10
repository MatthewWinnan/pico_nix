#include "mqtt_ha.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "lwip/dns.h"
#include "lwip/ip_addr.h"

// ---------------------------------------------------------------------------
// DNS resolution helper (synchronous wrapper around async lwIP DNS)
// ---------------------------------------------------------------------------

static volatile bool     s_dns_done;
static volatile ip_addr_t s_dns_result;

static void dns_found_cb(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name; (void)arg;
    if (addr) s_dns_result = *addr;
    else      IP_ADDR4((ip_addr_t *)&s_dns_result, 0, 0, 0, 0);
    s_dns_done = true;
}

static bool resolve_host(const char *host, ip_addr_t *out) {
    if (ipaddr_aton(host, out)) return true;

    s_dns_done = false;
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(host, out, dns_found_cb, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) return true;
    if (err != ERR_INPROGRESS) return false;

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!s_dns_done) {
        if (to_ms_since_boot(get_absolute_time()) - start > 5000) return false;
        sleep_ms(50);
    }
    *out = s_dns_result;
    return !ip_addr_isany(out);
}

// ---------------------------------------------------------------------------
// GPS incoming-publish callbacks (called from IRQ / lwIP context)
// ---------------------------------------------------------------------------

static char     s_gps_buf[256];
static uint16_t s_gps_buf_len;
static bool     s_gps_active;

static void inpub_request_cb(void *arg, const char *topic, u32_t tot_len) {
    (void)tot_len;
    s_gps_active  = (strcmp(topic, MQTT_GPS_TOPIC) == 0);
    s_gps_buf_len = 0;
}

static void inpub_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    mqtt_ha_t *ctx = (mqtt_ha_t *)arg;

    if (!s_gps_active) return;

    if (s_gps_buf_len + len < (uint16_t)(sizeof(s_gps_buf) - 1)) {
        memcpy(s_gps_buf + s_gps_buf_len, data, len);
        s_gps_buf_len += len;
    }

    if (!(flags & MQTT_DATA_FLAG_LAST)) return;

    s_gps_buf[s_gps_buf_len] = '\0';
    char *p = strstr(s_gps_buf, "\"alt\":");
    if (p) {
        float alt = strtof(p + 6, NULL);
        ctx->gps_altitude_m = alt;
        ctx->gps_alt_valid  = true;
    }
}

// ---------------------------------------------------------------------------
// MQTT connection callback (IRQ context)
// ---------------------------------------------------------------------------

static void connection_cb(mqtt_client_t *client, void *arg,
                          mqtt_connection_status_t status) {
    (void)client;
    mqtt_ha_t *ctx = (mqtt_ha_t *)arg;
    ctx->connected = (status == MQTT_CONNECT_ACCEPTED);
    if (!ctx->connected) {
        printf("MQTT disconnected (status %d)\r\n", (int)status);
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool mqtt_ha_connect(mqtt_ha_t *ctx, const char *host, uint16_t port) {
    ctx->client        = mqtt_client_new();
    ctx->connected     = false;
    ctx->gps_alt_valid = false;
    ctx->gps_altitude_m = 0.0f;

    if (!ctx->client) {
        printf("mqtt_client_new failed\r\n");
        return false;
    }

    ip_addr_t server_ip;
    printf("Resolving MQTT host '%s'...\r\n", host);
    if (!resolve_host(host, &server_ip)) {
        printf("DNS resolution failed\r\n");
        return false;
    }

    // No LWT — expire_after in the discovery config handles unavailability.
    struct mqtt_connect_client_info_t ci = {
        .client_id   = "pico_weather_aux",
        .client_user = NULL,
        .client_pass = NULL,
        .keep_alive  = 60,
        .will_topic  = NULL,
        .will_msg    = NULL,
        .will_qos    = 0,
        .will_retain = 0,
    };

    cyw43_arch_lwip_begin();
    err_t err = mqtt_client_connect(ctx->client, &server_ip, port,
                                    connection_cb, ctx, &ci);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("mqtt_client_connect error %d\r\n", (int)err);
        return false;
    }

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!ctx->connected) {
        if (to_ms_since_boot(get_absolute_time()) - start > 15000) {
            printf("MQTT connect timeout\r\n");
            return false;
        }
        sleep_ms(100);
    }

    printf("MQTT connected to %s:%u\r\n", host, port);
    return true;
}

// ---------------------------------------------------------------------------
// Discovery helper — publishes one retained sensor config to HA.
// state_topic:    the topic HA should subscribe to for state updates
// value_template: full Jinja2 template string for extracting the value
// ---------------------------------------------------------------------------

static void publish_discovery(mqtt_ha_t *ctx,
                              const char *object_id,
                              const char *name,
                              const char *device_class,  // nullable
                              const char *unit,
                              const char *state_topic,
                              const char *value_template,
                              uint32_t    expire_after_s) {
    char topic[128];
    snprintf(topic, sizeof(topic),
             HA_DISC_PREFIX "/sensor/%s/config", object_id);

    char dc_field[64] = "";
    if (device_class) {
        snprintf(dc_field, sizeof(dc_field),
                 "\"device_class\":\"%s\",", device_class);
    }

    char payload[800];
    snprintf(payload, sizeof(payload),
             "{"
             "\"name\":\"%s\","
             "%s"
             "\"object_id\":\"%s\","
             "\"state_topic\":\"%s\","
             "\"unit_of_measurement\":\"%s\","
             "\"value_template\":\"%s\","
             "\"unique_id\":\"%s\","
             "\"expire_after\":%lu,"
             "\"availability_topic\":\"%s\","
             "\"payload_available\":\"online\","
             "\"payload_not_available\":\"offline\","
             "\"device\":{"
               "\"identifiers\":[\"pico_weather_aux\"],"
               "\"name\":\"Pico W Aux Weather Station\","
               "\"model\":\"BMP180 + BME280 + INA219\","
               "\"manufacturer\":\"Bosch / Texas Instruments\""
             "}"
             "}",
             name, dc_field, object_id, state_topic, unit,
             value_template, object_id, (unsigned long)expire_after_s,
             MQTT_STATUS_TOPIC);

    cyw43_arch_lwip_begin();
    mqtt_publish(ctx->client, topic,
                 payload, strlen(payload),
                 0 /*qos*/, 1 /*retain*/, NULL, NULL);
    cyw43_arch_lwip_end();
}

void mqtt_ha_publish_discovery(mqtt_ha_t *ctx, uint32_t expire_after_s) {
// st = state_topic, vt = value_template
#define PD(ctx, id, name, dc, unit, st, vt) \
    publish_discovery(ctx, id, name, dc, unit, st, vt, expire_after_s); \
    sleep_ms(200)

    // ── BMP180 (10-min averages on state topic) ────────────────────────────────
    PD(ctx, "bmp180_temperature",  "BMP180 Temperature",        "temperature",          "\xC2\xB0" "C",
       MQTT_STATE_TOPIC, "{{ value_json.bmp180_temperature }}");
    PD(ctx, "bmp180_pressure",     "BMP180 Pressure (QFE)",     "atmospheric_pressure", "hPa",
       MQTT_STATE_TOPIC, "{{ value_json.bmp180_pressure }}");
    PD(ctx, "bmp180_pressure_msl", "BMP180 Pressure MSL (QNH)", "atmospheric_pressure", "hPa",
       MQTT_STATE_TOPIC, "{{ value_json.bmp180_pressure_msl }}");
    PD(ctx, "bmp180_altitude",     "BMP180 Altitude",           NULL,                   "m",
       MQTT_STATE_TOPIC, "{{ value_json.bmp180_altitude }}");

    // ── BME280 (10-min averages on state topic) ────────────────────────────────
    PD(ctx, "bme280_temperature",  "BME280 Temperature",        "temperature",          "\xC2\xB0" "C",
       MQTT_STATE_TOPIC, "{{ value_json.bme280_temperature }}");
    PD(ctx, "bme280_pressure",     "BME280 Pressure (QFE)",     "atmospheric_pressure", "hPa",
       MQTT_STATE_TOPIC, "{{ value_json.bme280_pressure }}");
    PD(ctx, "bme280_pressure_msl", "BME280 Pressure MSL (QNH)", "atmospheric_pressure", "hPa",
       MQTT_STATE_TOPIC, "{{ value_json.bme280_pressure_msl }}");
    PD(ctx, "bme280_altitude",     "BME280 Altitude",           NULL,                   "m",
       MQTT_STATE_TOPIC, "{{ value_json.bme280_altitude }}");
    PD(ctx, "bme280_humidity",     "BME280 Humidity",           "humidity",             "%",
       MQTT_STATE_TOPIC, "{{ value_json.bme280_humidity }}");

    // ── INA219 / Pico-UPS-A (10-min averages on state topic) ──────────────────
    PD(ctx, "pico_w_battery", "Battery",         "battery", "%",
       MQTT_STATE_TOPIC, "{{ value_json.battery }}");
    PD(ctx, "pico_w_voltage", "Battery Voltage", "voltage", "V",
       MQTT_STATE_TOPIC, "{{ value_json.voltage }}");
    PD(ctx, "pico_w_current", "Battery Current", "current", "mA",
       MQTT_STATE_TOPIC, "{{ value_json.current }}");

    // ── High-resolution pressure (1-min samples on hires topic) ───────────────
    PD(ctx, "bmp180_press_hires",     "BMP180 Pressure hires (QFE)",     "atmospheric_pressure", "hPa",
       MQTT_PRESS_HIRES_TOPIC, "{{ value_json.bmp_pa }}");
    PD(ctx, "bmp180_press_msl_hires", "BMP180 Pressure MSL hires (QNH)", "atmospheric_pressure", "hPa",
       MQTT_PRESS_HIRES_TOPIC, "{{ value_json.bmp_msl_pa }}");
    PD(ctx, "bme280_press_hires",     "BME280 Pressure hires (QFE)",     "atmospheric_pressure", "hPa",
       MQTT_PRESS_HIRES_TOPIC, "{{ value_json.bme_pa }}");
    PD(ctx, "bme280_press_msl_hires", "BME280 Pressure MSL hires (QNH)", "atmospheric_pressure", "hPa",
       MQTT_PRESS_HIRES_TOPIC, "{{ value_json.bme_msl_pa }}");

    // ── Pressure tendency (3-hour change + WMO characteristic, BME280) ───────
    // All three return None (→ unknown state) until 3 hours of data accumulate.
    PD(ctx, "bme280_tendency",        "Pressure Tendency (3h)",      NULL, "hPa/3h",
       MQTT_STATE_TOPIC, "{{ value_json.tendency | default(None) }}");
    PD(ctx, "bme280_tendency_a",      "Pressure Tendency Code (WMO)",  NULL, "",
       MQTT_STATE_TOPIC, "{{ value_json.tendency_a | default(None) }}");
    PD(ctx, "bme280_tendency_a_desc", "Pressure Tendency Description", NULL, "",
       MQTT_STATE_TOPIC, "{{ value_json.tendency_a_desc | default(None) }}");

#undef PD
}

bool mqtt_ha_fetch_gps_altitude(mqtt_ha_t *ctx, float *alt_m) {
    ctx->gps_alt_valid  = false;
    ctx->gps_altitude_m = 0.0f;

    cyw43_arch_lwip_begin();
    mqtt_set_inpub_callback(ctx->client, inpub_request_cb, inpub_data_cb, ctx);
    mqtt_subscribe(ctx->client, MQTT_GPS_TOPIC, 0 /*qos*/, NULL, NULL);
    cyw43_arch_lwip_end();

    uint32_t start = to_ms_since_boot(get_absolute_time());
    while (!ctx->gps_alt_valid) {
        if (to_ms_since_boot(get_absolute_time()) - start > 3000) break;
        sleep_ms(50);
    }

    cyw43_arch_lwip_begin();
    mqtt_unsubscribe(ctx->client, MQTT_GPS_TOPIC, NULL, NULL);
    cyw43_arch_lwip_end();

    if (ctx->gps_alt_valid && alt_m) {
        *alt_m = ctx->gps_altitude_m;
        return true;
    }
    return false;
}

void mqtt_ha_disconnect(mqtt_ha_t *ctx) {
    if (!ctx->client) return;
    cyw43_arch_lwip_begin();
    mqtt_disconnect(ctx->client);
    cyw43_arch_lwip_end();
    sleep_ms(100);
    cyw43_arch_lwip_begin();
    mqtt_client_free(ctx->client);
    cyw43_arch_lwip_end();
    ctx->client = NULL;
}

// Human-readable descriptions for WMO pressure tendency characteristic codes
// (WMO code table 0200). Indexed by tendency_a (0-8).
static const char *const s_tend_a_desc[9] = {
    "Rising then falling",  // 0: increasing then decreasing; net same or higher
    "Rising, slowing",      // 1: increasing then steady or more slowly
    "Rising",               // 2: increasing steadily or unsteadily
    "Rising rapidly",       // 3: accelerating rise, or steady/falling then rising
    "Steady",               // 4: little or no change
    "Falling then rising",  // 5: decreasing then increasing; net same or lower
    "Falling, slowing",     // 6: decreasing then steady or more slowly
    "Falling",              // 7: decreasing steadily or unsteadily
    "Falling rapidly",      // 8: accelerating fall, or steady/rising then falling
};

void mqtt_ha_publish_state(mqtt_ha_t *ctx, const sensor_state_t *s) {
    // Build optional tendency fields — omit entirely when not yet valid so that
    // HA's {{ value_json.x | default(None) }} returns None (unknown state).
    char tend_str[96] = "";
    if (s->tendency_valid && s->tendency_a >= 0 && s->tendency_a <= 8) {
        snprintf(tend_str, sizeof(tend_str),
                 ",\"tendency\":%.1f,\"tendency_a\":%d,\"tendency_a_desc\":\"%s\"",
                 s->bme280_tendency_hpa, s->tendency_a,
                 s_tend_a_desc[s->tendency_a]);
    }

    char payload[600];
    snprintf(payload, sizeof(payload),
             "{"
             "\"bmp180_temperature\":%.1f"
             ",\"bmp180_pressure\":%.2f,\"bmp180_pressure_msl\":%.2f"
             ",\"bmp180_altitude\":%.1f"
             ",\"bme280_temperature\":%.1f"
             ",\"bme280_pressure\":%.2f,\"bme280_pressure_msl\":%.2f"
             ",\"bme280_altitude\":%.1f"
             ",\"bme280_humidity\":%.1f"
             ",\"battery\":%d,\"voltage\":%.2f,\"current\":%.1f"
             "%s"
             "}",
             s->bmp180_temp_c,
             s->bmp180_pressure_pa     / 100.0f,
             s->bmp180_pressure_msl_pa / 100.0f,
             s->bmp180_altitude_m,
             s->bme280_temp_c,
             s->bme280_pressure_pa     / 100.0f,
             s->bme280_pressure_msl_pa / 100.0f,
             s->bme280_altitude_m,
             s->bme280_humidity_pct,
             s->battery_pct, s->voltage_v, s->current_ma,
             tend_str);

    cyw43_arch_lwip_begin();
    mqtt_publish(ctx->client, MQTT_STATE_TOPIC,
                 payload, strlen(payload),
                 0 /*qos*/, 0 /*retain*/, NULL, NULL);
    cyw43_arch_lwip_end();
}

void mqtt_ha_publish_press_hires(mqtt_ha_t *ctx,
                                 const press_hires_t *readings, int count) {
    char payload[128];
    for (int i = 0; i < count; i++) {
        snprintf(payload, sizeof(payload),
                 "{\"bmp_pa\":%.2f,\"bmp_msl_pa\":%.2f"
                 ",\"bme_pa\":%.2f,\"bme_msl_pa\":%.2f}",
                 readings[i].bmp_pa,     readings[i].bmp_msl_pa,
                 readings[i].bme_pa,     readings[i].bme_msl_pa);
        cyw43_arch_lwip_begin();
        mqtt_publish(ctx->client, MQTT_PRESS_HIRES_TOPIC,
                     payload, strlen(payload),
                     0 /*qos*/, 0 /*retain*/, NULL, NULL);
        cyw43_arch_lwip_end();
        sleep_ms(20);
    }
}

void mqtt_ha_publish_status(mqtt_ha_t *ctx, bool online) {
    const char *msg = online ? "online" : "offline";
    cyw43_arch_lwip_begin();
    mqtt_publish(ctx->client, MQTT_STATUS_TOPIC,
                 msg, strlen(msg),
                 0 /*qos*/, 1 /*retain*/, NULL, NULL);
    cyw43_arch_lwip_end();
}
