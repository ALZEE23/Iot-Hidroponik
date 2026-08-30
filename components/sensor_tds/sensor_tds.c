#include "sensor_tds.h"
#include "adc_shared.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_log.h"

static const char *TAG = "sensor_tds";

#define TDS_ADC_UNIT     ADC_UNIT_1
#define TDS_ADC_CHANNEL  ADC_CHANNEL_3 // GPIO39 / SENSOR_VN
#define TDS_ADC_ATTEN    ADC_ATTEN_DB_12

static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_cali_handle;

esp_err_t sensor_tds_init(void)
{
    esp_err_t err = adc_shared_get_unit1(&s_adc_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal ambil ADC1 unit: %s", esp_err_to_name(err));
        return err;
    }

    adc_oneshot_chan_cfg_t chan_config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = TDS_ADC_ATTEN,
    };
    err = adc_oneshot_config_channel(s_adc_handle, TDS_ADC_CHANNEL, &chan_config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal config channel: %s", esp_err_to_name(err));
        return err;
    }

    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = TDS_ADC_UNIT,
        .atten = TDS_ADC_ATTEN,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    err = adc_cali_create_scheme_line_fitting(&cali_config, &s_cali_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "kalibrasi ADC nggak tersedia, pakai raw->voltage kasar: %s", esp_err_to_name(err));
        s_cali_handle = NULL;
    }

    ESP_LOGI(TAG, "sensor TDS siap di ADC1 channel %d", TDS_ADC_CHANNEL);
    return ESP_OK;
}

esp_err_t sensor_tds_read(float water_temp_c, float *tds_ppm)
{
    if (s_adc_handle == NULL || tds_ppm == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    int raw = 0;
    esp_err_t err = adc_oneshot_read(s_adc_handle, TDS_ADC_CHANNEL, &raw);
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
        mv = (raw * 3300) / 4095;
    }

    float voltage = mv / 1000.0f;
    // formula standar DFRobot Gravity TDS, dikompensasi suhu terhadap 25C
    float compensation_coefficient = 1.0f + 0.02f * (water_temp_c - 25.0f);
    float compensation_voltage = voltage / compensation_coefficient;
    *tds_ppm = (133.42f * compensation_voltage * compensation_voltage * compensation_voltage
                - 255.86f * compensation_voltage * compensation_voltage
                + 857.39f * compensation_voltage) * 0.5f;

    if (*tds_ppm < 0) {
        *tds_ppm = 0;
    }

    return ESP_OK;
}
