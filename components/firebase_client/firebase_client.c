#include <stdio.h>
#include <string.h>
#include <inttypes.h>
#include "firebase_client.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_log.h"

static const char *TAG = "firebase_client";

static esp_err_t put_json(const char *url, const char *json_payload)
{
    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (client == NULL) {
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, json_payload, strlen(json_payload));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        if (status < 200 || status >= 300) {
            ESP_LOGW(TAG, "Firebase PUT %s -> HTTP %d", url, status);
            err = ESP_FAIL;
        }
    } else {
        ESP_LOGW(TAG, "Firebase PUT %s gagal: %s", url, esp_err_to_name(err));
    }

    esp_http_client_cleanup(client);
    return err;
}

esp_err_t firebase_client_push(const char *json_payload, time_t ts)
{
    if (strlen(CONFIG_FIREBASE_DATABASE_HOST) == 0) {
        ESP_LOGD(TAG, "CONFIG_FIREBASE_DATABASE_HOST belum diisi, skip push");
        return ESP_ERR_INVALID_STATE;
    }

    char url[256];

    snprintf(url, sizeof(url), "https://%s/%s/latest.json",
             CONFIG_FIREBASE_DATABASE_HOST, CONFIG_FIREBASE_PATH_PREFIX);
    esp_err_t err_latest = put_json(url, json_payload);

    snprintf(url, sizeof(url), "https://%s/%s/history/%" PRId64 ".json",
             CONFIG_FIREBASE_DATABASE_HOST, CONFIG_FIREBASE_PATH_PREFIX, (int64_t) ts);
    esp_err_t err_history = put_json(url, json_payload);

    return (err_latest == ESP_OK && err_history == ESP_OK) ? ESP_OK : ESP_FAIL;
}
