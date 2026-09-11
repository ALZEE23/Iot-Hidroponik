#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_log.h"

#include "wifi_manager.h"
#include "sensor_hub.h"
#include "sensor_ds18b20.h"
#include "sensor_dht22.h"
#include "sensor_ph.h"
#include "sensor_tds.h"
#include "sensor_ultrasonic.h"
#include "mqtt_client_app.h"
#include "firebase_client.h"
#include "local_webserver.h"

static const char *TAG = "main";

static void sensor_task(void *arg)
{
    int reads_since_last_push = 0;

    while (1) {
        sensor_reading_t reading = {0};

        float water_temp = 0;
        if (sensor_ds18b20_read(&water_temp) == ESP_OK) {
            reading.water_temp_c = water_temp;
            reading.water_temp_valid = true;
        } else {
            ESP_LOGW(TAG, "gagal baca DS18B20");
        }

        float air_temp = 0, air_hum = 0;
        if (sensor_dht22_read(&air_temp, &air_hum) == ESP_OK) {
            reading.air_temp_c = air_temp;
            reading.air_humidity_pct = air_hum;
            reading.air_valid = true;
        } else {
            ESP_LOGW(TAG, "gagal baca DHT22");
        }

        float ph = 0;
        if (sensor_ph_read(&ph) == ESP_OK) {
            reading.ph = ph;
            reading.ph_valid = true;
        } else {
            ESP_LOGW(TAG, "gagal baca sensor pH");
        }

        float tds = 0;
        float temp_for_compensation = reading.water_temp_valid ? reading.water_temp_c : 25.0f;
        if (sensor_tds_read(temp_for_compensation, &tds) == ESP_OK) {
            reading.tds_ppm = tds;
            reading.tds_valid = true;
        } else {
            ESP_LOGW(TAG, "gagal baca sensor TDS");
        }

        // sensor ultrasonik dinonaktifkan sementara (hardware ECHO belum kedeteksi respons)

        reading.timestamp = time(NULL);
        sensor_hub_update(&reading);

        ESP_LOGI(TAG, "reading: water=%.2fC air=%.2fC/%.1f%% ph=%.2f tds=%.1fppm level=%.1fcm/%.0f%% sta=%s",
                 reading.water_temp_c, reading.air_temp_c, reading.air_humidity_pct,
                 reading.ph, reading.tds_ppm, reading.water_level_cm, reading.water_level_pct,
                 wifi_manager_is_sta_connected() ? "connected" : "disconnected");

        if (wifi_manager_is_sta_connected()) {
            char *json = sensor_hub_get_json();
            if (json != NULL) {
                mqtt_client_app_publish(json);

                reads_since_last_push++;
                if (reads_since_last_push >= CONFIG_FIREBASE_PUSH_INTERVAL_READS) {
                    reads_since_last_push = 0;
                    firebase_client_push(json, reading.timestamp);
                }
                free(json);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_SENSOR_READ_INTERVAL_SEC * 1000));
    }
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(sensor_hub_init());

    bool sta_connected_at_boot = wifi_manager_init();
    ESP_LOGI(TAG, "STA %s pas boot (AP tetap aktif terus)",
             sta_connected_at_boot ? "berhasil connect" : "belum connect, lanjut retry di background");

    if (sensor_ds18b20_init((gpio_num_t) CONFIG_SENSOR_DS18B20_GPIO) != ESP_OK) {
        ESP_LOGW(TAG, "DS18B20 nggak kedeteksi di GPIO%d, lanjut tanpa suhu air", CONFIG_SENSOR_DS18B20_GPIO);
    }
    sensor_dht22_init((gpio_num_t) CONFIG_SENSOR_DHT22_GPIO);
    sensor_ph_init();
    sensor_tds_init();
    // sensor_ultrasonic_init() dinonaktifkan sementara (hardware ECHO belum kedeteksi respons)

    // AP + local_webserver selalu aktif, MQTT jalan bareng begitu STA connect
    // (esp-mqtt auto-reconnect sendiri kalau STA belum/putus koneksi).
    local_webserver_start();
    mqtt_client_app_start();

    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);
}
