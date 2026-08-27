#pragma once

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// Simpan pin DHT22. Nggak butuh init hardware khusus (bus di-drive on-demand tiap baca).
esp_err_t sensor_dht22_init(gpio_num_t gpio);

// Bit-bang read DHT22 (protokol single-wire custom, bukan 1-Wire Dallas).
// Blocking ~5-25ms. Retry internal sampai 3x kalau checksum/timing gagal.
// DHT22 butuh jeda minimal ~2 detik antar pemanggilan (batasan datasheet) --
// jangan panggil fungsi ini lebih sering dari itu.
esp_err_t sensor_dht22_read(float *temp_c, float *humidity_pct);

#ifdef __cplusplus
}
#endif
