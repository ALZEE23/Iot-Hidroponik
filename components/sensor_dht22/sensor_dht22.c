#include "sensor_dht22.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_dht22";

static gpio_num_t s_gpio = GPIO_NUM_NC;

#define PULSE_TIMEOUT_US 200

esp_err_t sensor_dht22_init(gpio_num_t gpio)
{
    s_gpio = gpio;
    gpio_reset_pin(gpio);
    gpio_set_direction(gpio, GPIO_MODE_INPUT);
    gpio_set_pull_mode(gpio, GPIO_PULLUP_ONLY);
    ESP_LOGI(TAG, "DHT22 siap di GPIO%d", gpio);
    return ESP_OK;
}

// Tunggu sampai level pin == `level`, return microdetik yang dihabiskan menunggu,
// atau -1 kalau timeout (PULSE_TIMEOUT_US terlampaui).
static inline int64_t wait_for_level(int level)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_gpio) != level) {
        if (esp_timer_get_time() - start > PULSE_TIMEOUT_US) {
            return -1;
        }
    }
    return esp_timer_get_time() - start;
}

static esp_err_t dht22_read_once(float *temp_c, float *humidity_pct)
{
    uint8_t data[5] = {0};

    // --- start signal ---
    gpio_set_direction(s_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(s_gpio, 0);
    esp_rom_delay_us(1100);
    gpio_set_level(s_gpio, 1);
    esp_rom_delay_us(30);
    gpio_set_direction(s_gpio, GPIO_MODE_INPUT);

    // --- sensor response: low ~80us lalu high ~80us ---
    if (wait_for_level(0) < 0) {
        ESP_LOGD(TAG, "timeout nunggu response low");
        return ESP_ERR_TIMEOUT;
    }
    if (wait_for_level(1) < 0) {
        ESP_LOGD(TAG, "timeout nunggu response high");
        return ESP_ERR_TIMEOUT;
    }

    // --- 40 bit data ---
    // Tiap bit: sync low ~50us (durasinya konstan, nggak dipakai buat nentuin nilai bit)
    // diikuti pulsa high yang durasinya bervariasi -- itu yang nentuin 0/1.
    for (int bit = 0; bit < 40; bit++) {
        if (wait_for_level(0) < 0) {
            ESP_LOGD(TAG, "timeout nunggu awal sync-low bit %d", bit);
            return ESP_ERR_TIMEOUT;
        }
        if (wait_for_level(1) < 0) {
            ESP_LOGD(TAG, "timeout nunggu sync-low selesai bit %d", bit);
            return ESP_ERR_TIMEOUT;
        }
        int64_t high_us = wait_for_level(0);
        if (high_us < 0) {
            ESP_LOGD(TAG, "timeout nunggu pulsa high bit %d", bit);
            return ESP_ERR_TIMEOUT;
        }
        // pulsa high pendek (~26-28us) = bit 0, panjang (~70us) = bit 1
        data[bit / 8] <<= 1;
        if (high_us > 40) {
            data[bit / 8] |= 1;
        }
    }

    uint8_t checksum = (data[0] + data[1] + data[2] + data[3]) & 0xFF;
    if (checksum != data[4]) {
        ESP_LOGD(TAG, "checksum mismatch: got 0x%02x expect 0x%02x", data[4], checksum);
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t raw_humidity = ((uint16_t) data[0] << 8) | data[1];
    uint16_t raw_temp = ((uint16_t) (data[2] & 0x7F) << 8) | data[3];

    *humidity_pct = raw_humidity / 10.0f;
    *temp_c = raw_temp / 10.0f;
    if (data[2] & 0x80) {
        *temp_c = -(*temp_c);
    }

    return ESP_OK;
}

esp_err_t sensor_dht22_read(float *temp_c, float *humidity_pct)
{
    if (s_gpio == GPIO_NUM_NC || temp_c == NULL || humidity_pct == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ESP_FAIL;
    for (int attempt = 0; attempt < 2; attempt++) {
        err = dht22_read_once(temp_c, humidity_pct);
        if (err == ESP_OK) {
            return ESP_OK;
        }
        // DHT22 butuh minimal ~2 detik jeda sebelum siap direspons lagi (datasheet),
        // jeda lebih pendek dari itu bakal selalu gagal di "response low" (sensor belum siap).
        vTaskDelay(pdMS_TO_TICKS(2100));
    }

    ESP_LOGW(TAG, "gagal baca DHT22 setelah beberapa percobaan: %s", esp_err_to_name(err));
    return err;
}
