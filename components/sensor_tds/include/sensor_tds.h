#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init ADC1 channel 3 (GPIO39 / SENSOR_VN) buat modul TDS.
esp_err_t sensor_tds_init(void);

// Baca kepekatan nutrisi (ppm), dikompensasi suhu air `water_temp_c` (dari DS18B20)
// pakai formula polynomial standar sensor TDS analog (mis. DFRobot Gravity TDS).
esp_err_t sensor_tds_read(float water_temp_c, float *tds_ppm);

#ifdef __cplusplus
}
#endif
