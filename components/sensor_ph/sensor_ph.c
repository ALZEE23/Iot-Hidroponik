#include "sensor_ph.h"
#include "adc_shared.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "sensor_ph";

#define PH_ADC_UNIT     ADC_UNIT_1
#define PH_ADC_CHANNEL  ADC_CHANNEL_0 // GPIO36 / SENSOR_VP
#define PH_ADC_ATTEN    ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;

esp_err_t sensor_ph_init(void)
{
    esp_err_t err = adc_shared_get_unit1(&s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal ambil ADC1 unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = PH_ADC_ATTEN,
    };
    err = adc_oneshot_config_channel(s_adc_handle, PH_ADC_CHANNEL, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal config channel: %s", esp_err_to_name(err));
        return err;
    }

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = PH_ADC_UNIT,
        .atten = PH_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "kalibrasi ADC nggak tersedia, pakai raw->voltage kasar: %s", esp_err_to_name(err));
        s_cali_handle = NULL;
    }

    ESP_LOGI(TAG, "sensor pH siap di ADC1 channel %d", PH_ADC_CHANNEL);
    return ESP_OK;
}

esp_err_t sensor_ph_read(float *ph)
{
    if (s_adc_handle == NULL || ph == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, PH_ADC_CHANNEL, &raw);
    if (err != ESP_OK) {
        return err;
    }

    int mv = 0;
    if (s_cali_handle != NULL) {
        err = adc_cali_raw_to_voltage(s_cali_handle, raw, &mv);
        if (err != ESP_OK) {
            return err;
        }
    } else {
        // fallback kasar tanpa kalibrasi eFuse: asumsi rentang penuh 12-bit == atten 12dB (~3.3V)
        mv = (raw * 3300) / 4095;
    }

    ESP_LOGI(TAG, "raw=%d mv=%d", raw, mv);

    *ph = 7.0f - ((float) mv - CONFIG_PH_NEUTRAL_MV) / CONFIG_PH_MV_PER_PH;
    return ESP_OK;
}
