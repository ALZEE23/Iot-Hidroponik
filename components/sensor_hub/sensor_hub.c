#include <string.h>
#include "sensor_hub.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cJSON.h"

static const char *TAG = "sensor_hub";

static SemaphoreHandle_t s_mutex;
static sensor_reading_t s_latest;

esp_err_t sensor_hub_init(void)
{
    if (s_mutex != NULL) {
        return ESP_OK;
    }
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        ESP_LOGE(TAG, "gagal membuat mutex");
        return ESP_ERR_NO_MEM;
    }
    memset(&s_latest, 0, sizeof(s_latest));
    return ESP_OK;
}

void sensor_hub_update(const sensor_reading_t *reading)
{
    if (s_mutex == NULL || reading == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    s_latest = *reading;
    xSemaphoreGive(s_mutex);
}

void sensor_hub_get(sensor_reading_t *out)
{
    if (s_mutex == NULL || out == NULL) {
        return;
    }
    xSemaphoreTake(s_mutex, portMAX_DELAY);
    *out = s_latest;
    xSemaphoreGive(s_mutex);
}

char *sensor_hub_get_json(void)
{
    sensor_reading_t r;
    sensor_hub_get(&r);

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        return NULL;
    }

    if (r.water_temp_valid) {
        cJSON_AddNumberToObject(root, "water_temp_c", r.water_temp_c);
    } else {
        cJSON_AddNullToObject(root, "water_temp_c");
    }

    if (r.air_valid) {
        cJSON_AddNumberToObject(root, "air_temp_c", r.air_temp_c);
        cJSON_AddNumberToObject(root, "air_humidity_pct", r.air_humidity_pct);
    } else {
        cJSON_AddNullToObject(root, "air_temp_c");
        cJSON_AddNullToObject(root, "air_humidity_pct");
    }

    if (r.ph_valid) {
        cJSON_AddNumberToObject(root, "ph", r.ph);
    } else {
        cJSON_AddNullToObject(root, "ph");
    }

    if (r.tds_valid) {
        cJSON_AddNumberToObject(root, "tds_ppm", r.tds_ppm);
    } else {
        cJSON_AddNullToObject(root, "tds_ppm");
    }

    if (r.water_level_valid) {
        cJSON_AddNumberToObject(root, "water_level_cm", r.water_level_cm);
        cJSON_AddNumberToObject(root, "water_level_pct", r.water_level_pct);
    } else {
        cJSON_AddNullToObject(root, "water_level_cm");
        cJSON_AddNullToObject(root, "water_level_pct");
    }

    cJSON_AddNumberToObject(root, "timestamp", (double) r.timestamp);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    return json;
}
