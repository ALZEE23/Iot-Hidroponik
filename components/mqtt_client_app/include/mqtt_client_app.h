#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Connect ke HiveMQ Cloud (mqtts://, TLS via cert bundle bawaan ESP-IDF) pakai
// kredensial dari Kconfig. Non-blocking, reconnect otomatis ditangani esp-mqtt.
esp_err_t mqtt_client_app_start(void);

// Publish JSON payload ke CONFIG_MQTT_TOPIC. No-op (return ESP_ERR_INVALID_STATE)
// kalau client belum connect ke broker.
esp_err_t mqtt_client_app_publish(const char *json_payload);

#ifdef __cplusplus
}
#endif
