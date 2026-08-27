#include "sensor_ds18b20.h"
#include "esp_log.h"
#include "onewire_bus.h"
#include "ds18b20.h"

static const char *TAG = "sensor_ds18b20";

static onewire_bus_handle_t s_bus;
static ds18b20_device_handle_t s_device;

esp_err_t sensor_ds18b20_init(gpio_num_t gpio)
{
    onewire_bus_config_t bus_config = {
        .bus_gpio_num = gpio,
        .flags = { .en_pull_up = true },
    };
    onewire_bus_rmt_config_t rmt_config = {
        .max_rx_bytes = 10,
    };
    esp_err_t err = onewire_new_bus_rmt(&bus_config, &rmt_config, &s_bus);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal bikin 1-wire bus di GPIO%d: %s", gpio, esp_err_to_name(err));
        return err;
    }

    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t next_device;
    err = onewire_new_device_iter(s_bus, &iter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal bikin device iterator: %s", esp_err_to_name(err));
        return err;
    }

    esp_err_t search_result = onewire_device_iter_get_next(iter, &next_device);
    onewire_del_device_iter(iter);

    if (search_result != ESP_OK) {
        ESP_LOGE(TAG, "nggak ada device DS18B20 kedeteksi di GPIO%d", gpio);
        return ESP_ERR_NOT_FOUND;
    }

    ds18b20_config_t ds_cfg = {};
    err = ds18b20_new_device_from_enumeration(&next_device, &ds_cfg, &s_device);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal init DS18B20 device: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "DS18B20 siap di GPIO%d", gpio);
    return ESP_OK;
}

esp_err_t sensor_ds18b20_read(float *celsius)
{
    if (s_device == NULL || celsius == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ds18b20_trigger_temperature_conversion_for_all(s_bus);
    if (err != ESP_OK) {
        return err;
    }

    return ds18b20_get_temperature(s_device, celsius);
}
