#pragma once

#include <stdbool.h>
#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    float water_temp_c;     // DS18B20
    bool  water_temp_valid;

    float air_temp_c;       // DHT22
    float air_humidity_pct; // DHT22
    bool  air_valid;

    float ph;                // pH module
    bool  ph_valid;

    float tds_ppm;           // TDS module
    bool  tds_valid;

    float water_level_cm;    // HC-SR04, jarak sensor ke permukaan air
    float water_level_pct;   // HC-SR04, dikalibrasi ke 0-100% dari Kconfig
    bool  water_level_valid;

    time_t timestamp;        // unix time (0 kalau belum SNTP sync)
} sensor_reading_t;

// Wajib dipanggil sekali di awal app_main sebelum thread lain mengakses hub.
esp_err_t sensor_hub_init(void);

// Dipanggil oleh sensor_task setiap selesai membaca semua sensor.
void sensor_hub_update(const sensor_reading_t *reading);

// Dipanggil oleh local_webserver / publisher lain untuk snapshot terakhir.
void sensor_hub_get(sensor_reading_t *out);

// Serialize snapshot terakhir ke JSON string (cJSON_Print). Caller wajib free().
// Return NULL kalau alokasi gagal.
char *sensor_hub_get_json(void);

#ifdef __cplusplus
}
#endif
