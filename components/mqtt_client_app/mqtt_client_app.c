#include <stdio.h>
#include <string.h>
#include "mqtt_client_app.h"
#include "mqtt_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "mqtt_client_app";

static esp_mqtt_client_handle_t s_client;
static bool s_connected;

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                                int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t) event_data;
    switch ((esp_mqtt_event_id_t) event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "connected ke HiveMQ Cloud");
        s_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGW(TAG, "disconnect dari broker");
        s_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR, tls_last_esp_err=0x%x",
                 event->error_handle->esp_tls_last_esp_err);
        break;
    default:
        break;
    }
}

esp_err_t mqtt_client_app_start(void)
{
    if (strlen(CONFIG_MQTT_BROKER_HOST) == 0) {
        ESP_LOGI(TAG, "CONFIG_MQTT_BROKER_HOST belum diisi, skip start MQTT client");
        return ESP_ERR_INVALID_STATE;
    }

    char uri[128];
    snprintf(uri, sizeof(uri), "mqtts://%s:%d", CONFIG_MQTT_BROKER_HOST, CONFIG_MQTT_BROKER_PORT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = uri,
        .broker.verification.crt_bundle_attach = esp_crt_bundle_attach,
        .credentials.username = CONFIG_MQTT_USERNAME,
        .credentials.authentication.password = CONFIG_MQTT_PASSWORD,
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "gagal init mqtt client");
        return ESP_FAIL;
    }

    esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    return esp_mqtt_client_start(s_client);
}

esp_err_t mqtt_client_app_publish(const char *json_payload)
{
    if (s_client == NULL || !s_connected) {
        return ESP_ERR_INVALID_STATE;
    }
    int msg_id = esp_mqtt_client_publish(s_client, CONFIG_MQTT_TOPIC, json_payload, 0, 1, 0);
    return (msg_id >= 0) ? ESP_OK : ESP_FAIL;
}
