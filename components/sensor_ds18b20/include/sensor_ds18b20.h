#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Inisialisasi 1-Wire bus di `gpio` dan cari device DS18B20 pertama yang nyambung.
// Return ESP_ERR_NOT_FOUND kalau nggak ada device di bus.
esp_err_t sensor_ds18b20_init(gpio_num_t gpio);

// Trigger konversi + baca suhu air (celsius). Blocking ~750ms (waktu konversi 12-bit DS18B20).
esp_err_t sensor_ds18b20_read(float *celsius);

#ifdef __cplusplus
}
#endif
