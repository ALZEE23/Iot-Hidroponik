#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Init ADC1 channel 0 (GPIO36 / SENSOR_VP) buat modul pH (PH-4502C).
esp_err_t sensor_ph_init(void);

// Baca tegangan analog sensor pH lalu konversi ke nilai pH pakai kalibrasi linear
// dari Kconfig (CONFIG_PH_NEUTRAL_MV, CONFIG_PH_MV_PER_PH).
//
// PENTING: default kalibrasi di Kconfig cuma perkiraan kasar, WAJIB dikalibrasi
// ulang secara fisik pakai larutan buffer pH 4/7/10 sebelum dipakai beneran.
esp_err_t sensor_ph_read(float *ph);

#ifdef __cplusplus
}
#endif
