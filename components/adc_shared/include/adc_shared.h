#pragma once

#include "esp_err.h"
#include "esp_adc/adc_oneshot.h"

#ifdef __cplusplus
extern "C" {
#endif

// pH (ADC1 channel 0) & TDS (ADC1 channel 3) sama-sama nempel di ADC_UNIT_1.
// esp_adc oneshot cuma ngebolehin 1 handle per unit, jadi handle-nya di-share
// di sini (lazy-created sekali, thread-safe) bukan dibikin sendiri-sendiri per sensor.
esp_err_t adc_shared_get_unit1(adc_oneshot_unit_handle_t *out_handle);

#ifdef __cplusplus
}
#endif
