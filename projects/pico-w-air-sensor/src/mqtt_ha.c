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

static volatile bool      s_dns_done;
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
    ctx->client    = mqtt_client_new();
    ctx->connected = false;

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
        .client_id   = "pico_air_sensor",
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
// Pass device_class = NULL to omit it.
static void publish_discovery(mqtt_ha_t *ctx,
                              const char *object_id,
                              const char *name,
                              const char *device_class,  // nullable
                              const char *unit,
                              const char *value_key) {
    char topic[128];
    snprintf(topic, sizeof(topic),
             HA_DISC_PREFIX "/sensor/%s/config", object_id);

    char dc_field[64] = "";
    if (device_class) {
        snprintf(dc_field, sizeof(dc_field),
                 "\"device_class\":\"%s\",", device_class);
    }

    char payload[768];
    snprintf(payload, sizeof(payload),
             "{"
             "\"name\":\"%s\","
             "%s"
             "\"object_id\":\"%s\","
             "\"state_topic\":\"%s\","
             "\"unit_of_measurement\":\"%s\","
             "\"value_template\":\"{{ value_json.%s }}\","
             "\"unique_id\":\"%s\","
             "\"availability_topic\":\"%s\","
             "\"payload_available\":\"online\","
             "\"payload_not_available\":\"offline\","
             "\"device\":{"
               "\"identifiers\":[\"pico_air_sensor\"],"
               "\"name\":\"Pico W Air Sensor\","
               "\"model\":\"PMSA003 + SSD1306\","
               "\"manufacturer\":\"Plantower\""
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
    // PM concentrations — HA has native device classes for these
    publish_discovery(ctx,
        "air_pm1_0", "PM1.0", "pm1", "\xc2\xb5g/m\xc2\xb3", "pm1_0");
    sleep_ms(200);

    publish_discovery(ctx,
        "air_pm2_5", "PM2.5", "pm25", "\xc2\xb5g/m\xc2\xb3", "pm2_5");
    sleep_ms(200);

    publish_discovery(ctx,
        "air_pm10", "PM10", "pm10", "\xc2\xb5g/m\xc2\xb3", "pm10");
    sleep_ms(200);

    // Particle counts — no standard HA device class
    publish_discovery(ctx,
        "air_cnt_03", "Particles >0.3\xc2\xb5m", NULL, "p/0.1L", "cnt_03");
    sleep_ms(200);

    publish_discovery(ctx,
        "air_cnt_05", "Particles >0.5\xc2\xb5m", NULL, "p/0.1L", "cnt_05");
    sleep_ms(200);

    publish_discovery(ctx,
        "air_cnt_10", "Particles >1.0\xc2\xb5m", NULL, "p/0.1L", "cnt_10");
    sleep_ms(200);

    publish_discovery(ctx,
        "air_cnt_25", "Particles >2.5\xc2\xb5m", NULL, "p/0.1L", "cnt_25");
    sleep_ms(200);

    publish_discovery(ctx,
        "air_cnt_50", "Particles >5.0\xc2\xb5m", NULL, "p/0.1L", "cnt_50");
    sleep_ms(200);

    publish_discovery(ctx,
        "air_cnt_100", "Particles >10\xc2\xb5m", NULL, "p/0.1L", "cnt_100");
    sleep_ms(200);
}

void mqtt_ha_publish_state(mqtt_ha_t *ctx, const pmsa003_data_t *d) {
    char payload[256];
    snprintf(payload, sizeof(payload),
             "{"
             "\"pm1_0\":%u,\"pm2_5\":%u,\"pm10\":%u,"
             "\"cnt_03\":%u,\"cnt_05\":%u,\"cnt_10\":%u,"
             "\"cnt_25\":%u,\"cnt_50\":%u,\"cnt_100\":%u"
             "}",
             d->pm10, d->pm25, d->pm100,
             d->cnt_03, d->cnt_05, d->cnt_10,
             d->cnt_25, d->cnt_50, d->cnt_100);

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
