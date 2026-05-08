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
    // Try literal IP first
    if (ipaddr_aton(host, out)) return true;

    s_dns_done = false;
    cyw43_arch_lwip_begin();
    err_t err = dns_gethostbyname(host, out, dns_found_cb, NULL);
    cyw43_arch_lwip_end();

    if (err == ERR_OK) return true;   // cached result
    if (err != ERR_INPROGRESS) return false;

    // Wait up to 5 s for the DNS callback
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
static bool     s_gps_active;   // true while receiving a GPS message

static void inpub_request_cb(void *arg, const char *topic, u32_t tot_len) {
    (void)tot_len;
    s_gps_active  = (strcmp(topic, MQTT_GPS_TOPIC) == 0);
    s_gps_buf_len = 0;
}

static void inpub_data_cb(void *arg, const u8_t *data, u16_t len, u8_t flags) {
    mqtt_ha_t *ctx = (mqtt_ha_t *)arg;

    if (!s_gps_active) return;

    // Accumulate payload into buffer (drop if overrun)
    if (s_gps_buf_len + len < (uint16_t)(sizeof(s_gps_buf) - 1)) {
        memcpy(s_gps_buf + s_gps_buf_len, data, len);
        s_gps_buf_len += len;
    }

    if (!(flags & MQTT_DATA_FLAG_LAST)) return;

    // Full message received — parse "alt" field from JSON
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

    struct mqtt_connect_client_info_t ci = {
        .client_id   = "pico_bmp180",
        .client_user = NULL,
        .client_pass = NULL,
        .keep_alive  = 60,
        .will_topic  = MQTT_STATUS_TOPIC,
        .will_msg    = "offline",
        .will_qos    = 0,
        .will_retain = 1,
    };

    cyw43_arch_lwip_begin();
    err_t err = mqtt_client_connect(ctx->client, &server_ip, port,
                                    connection_cb, ctx, &ci);
    cyw43_arch_lwip_end();

    if (err != ERR_OK) {
        printf("mqtt_client_connect error %d\r\n", (int)err);
        return false;
    }

    // Poll until connected or timeout (15 s)
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

// Helper: build and retain-publish one discovery config.
// Pass device_class = NULL to omit it (HA requires no device_class for
// quantities like altitude that have no matching class).
static void publish_discovery(mqtt_ha_t *ctx,
                              const char *object_id,
                              const char *name,
                              const char *device_class,  // nullable
                              const char *unit,
                              const char *value_key) {
    char topic[128];
    snprintf(topic, sizeof(topic),
             HA_DISC_PREFIX "/sensor/%s/config", object_id);

    // Conditionally include the device_class field
    char dc_field[64] = "";
    if (device_class) {
        snprintf(dc_field, sizeof(dc_field),
                 "\"device_class\":\"%s\",", device_class);
    }

    char payload[512];
    snprintf(payload, sizeof(payload),
             "{"
             "\"name\":\"%s\","
             "%s"                      // device_class field (may be empty)
             "\"object_id\":\"%s\","   // forces entity_id = sensor.<object_id>
             "\"state_topic\":\"%s\","
             "\"unit_of_measurement\":\"%s\","
             "\"value_template\":\"{{ value_json.%s }}\","
             "\"unique_id\":\"%s\","
             "\"availability_topic\":\"%s\","
             "\"payload_available\":\"online\","
             "\"payload_not_available\":\"offline\","
             "\"device\":{"
               "\"identifiers\":[\"bmp180_pico_w\"],"
               "\"name\":\"BMP180 Weather Station\","
               "\"model\":\"BMP180\","
               "\"manufacturer\":\"Bosch\""
             "}"
             "}",
             name, dc_field, object_id, MQTT_STATE_TOPIC, unit,
             value_key, object_id, MQTT_STATUS_TOPIC);

    cyw43_arch_lwip_begin();
    mqtt_publish(ctx->client, topic,
                 payload, strlen(payload),
                 0 /*qos*/, 1 /*retain*/, NULL, NULL);
    cyw43_arch_lwip_end();
}

void mqtt_ha_publish_discovery(mqtt_ha_t *ctx) {
    publish_discovery(ctx,
        "bmp180_temperature",
        "Temperature",
        "temperature",
        "\xC2\xB0" "C",  // °C — split so \xB0 doesn't consume 'C' as a hex digit
        "temperature");
    sleep_ms(200);  // let the ring buffer drain before queuing the next payload

    publish_discovery(ctx,
        "bmp180_pressure",
        "Pressure (QFE)",
        "atmospheric_pressure",
        "hPa",
        "pressure");
    sleep_ms(200);

    publish_discovery(ctx,
        "bmp180_pressure_msl",
        "Pressure MSL (QNH)",
        "atmospheric_pressure",
        "hPa",
        "pressure_msl");
    sleep_ms(200);

    // HA has no "altitude" device_class — omit it so HA accepts the config.
    publish_discovery(ctx,
        "bmp180_altitude",
        "Altitude (barometric)",
        NULL,
        "m",
        "altitude");
    sleep_ms(200);

    // INA219 / Pico-UPS-A
    publish_discovery(ctx,
        "pico_w_battery",
        "Battery",
        "battery",
        "%",
        "battery");
    sleep_ms(200);

    publish_discovery(ctx,
        "pico_w_voltage",
        "Battery Voltage",
        "voltage",
        "V",
        "voltage");
    sleep_ms(200);

    publish_discovery(ctx,
        "pico_w_current",
        "Battery Current",
        "current",
        "mA",
        "current");
    sleep_ms(200);
}

void mqtt_ha_subscribe_gps(mqtt_ha_t *ctx) {
    cyw43_arch_lwip_begin();
    // Install data callbacks before subscribing so no messages are missed
    mqtt_set_inpub_callback(ctx->client, inpub_request_cb, inpub_data_cb, ctx);
    mqtt_subscribe(ctx->client, MQTT_GPS_TOPIC, 0 /*qos*/, NULL, NULL);
    cyw43_arch_lwip_end();
}

void mqtt_ha_publish_state(mqtt_ha_t *ctx, const sensor_state_t *s) {
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{\"temperature\":%.1f"
             ",\"pressure\":%.2f,\"pressure_msl\":%.2f"
             ",\"altitude\":%.1f"
             ",\"battery\":%d,\"voltage\":%.2f,\"current\":%.1f}",
             s->temp_c,
             s->pressure_pa     / 100.0f,   // Pa → hPa
             s->pressure_msl_pa / 100.0f,
             s->altitude_m,
             s->battery_pct, s->voltage_v, s->current_ma);

    cyw43_arch_lwip_begin();
    mqtt_publish(ctx->client, MQTT_STATE_TOPIC,
                 payload, strlen(payload),
                 0 /*qos*/, 0 /*retain*/, NULL, NULL);
    cyw43_arch_lwip_end();
}

void mqtt_ha_publish_status(mqtt_ha_t *ctx, bool online) {
    const char *msg = online ? "online" : "offline";
    cyw43_arch_lwip_begin();
    mqtt_publish(ctx->client, MQTT_STATUS_TOPIC,
                 msg, strlen(msg),
                 0 /*qos*/, 1 /*retain*/, NULL, NULL);
    cyw43_arch_lwip_end();
}
