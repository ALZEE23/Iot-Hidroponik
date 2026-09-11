#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Simpan pin TRIG/ECHO HC-SR04 dan siapkan arah GPIO-nya.
// ECHO wajib lewat voltage divider (5V -> 3.3V) sebelum masuk pin ESP32.
esp_err_t sensor_ultrasonic_init(gpio_num_t trig_gpio, gpio_num_t echo_gpio);

// Trigger pulsa 10us lalu ukur lebar pulsa ECHO buat hitung jarak sensor ke permukaan air (cm).
// Blocking sampai ~30ms (timeout echo). Return ESP_ERR_TIMEOUT kalau nggak ada pantulan
// (echo nggak naik/turun dalam waktu wajar -- biasanya nggak ada objek dalam jangkauan).
esp_err_t sensor_ultrasonic_read_distance_cm(float *distance_cm);

// Konversi jarak (cm) ke persentase level air (0-100) pakai kalibrasi tangki dari Kconfig
// (CONFIG_WATER_TANK_EMPTY_DISTANCE_CM / CONFIG_WATER_TANK_FULL_DISTANCE_CM). Hasil di-clamp ke 0-100.
float sensor_ultrasonic_distance_to_level_pct(float distance_cm);

#ifdef __cplusplus
}
#endif
