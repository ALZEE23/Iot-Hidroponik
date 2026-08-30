#include "adc_shared.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static SemaphoreHandle_t s_mutex;
static adc_oneshot_unit_handle_t s_unit1_handle;

static SemaphoreHandle_t get_mutex(void)
{
    static portMUX_TYPE spinlock = portMUX_INITIALIZER_UNLOCKED;
    portENTER_CRITICAL(&spinlock);
    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
    }
    portEXIT_CRITICAL(&spinlock);
    return s_mutex;
}

esp_err_t adc_shared_get_unit1(adc_oneshot_unit_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    SemaphoreHandle_t mutex = get_mutex();
    xSemaphoreTake(mutex, portMAX_DELAY);

    esp_err_t err = ESP_OK;
    if (s_unit1_handle == NULL) {
        adc_oneshot_unit_init_cfg_t init_config = {
            .unit_id = ADC_UNIT_1,
        };
        err = adc_oneshot_new_unit(&init_config, &s_unit1_handle);
    }

    xSemaphoreGive(mutex);

    if (err == ESP_OK) {
        *out_handle = s_unit1_handle;
    }
    return err;
}
