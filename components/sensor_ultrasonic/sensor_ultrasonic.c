#include "sensor_ultrasonic.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "sensor_ultrasonic";

static gpio_num_t s_trig_gpio = GPIO_NUM_NC;
static gpio_num_t s_echo_gpio = GPIO_NUM_NC;

// HC-SR04 nggak balikin echo kalau nggak ada pantulan dalam jangkauan (~4-5m),
// jadi kedua tunggu ini dibatasi biar nggak stuck selamanya.
#define ECHO_START_TIMEOUT_US 30000
#define ECHO_PULSE_TIMEOUT_US 30000

// Kecepatan suara ~340m/s pulang-pergi -> cm = duration_us / 58.
#define US_TO_CM_DIVISOR 58.0f

esp_err_t sensor_ultrasonic_init(gpio_num_t trig_gpio, gpio_num_t echo_gpio)
{
    s_trig_gpio = trig_gpio;
    s_echo_gpio = echo_gpio;

    gpio_reset_pin(trig_gpio);
    gpio_set_direction(trig_gpio, GPIO_MODE_OUTPUT);
    gpio_set_level(trig_gpio, 0);

    gpio_reset_pin(echo_gpio);
    gpio_set_direction(echo_gpio, GPIO_MODE_INPUT);

    ESP_LOGI(TAG, "HC-SR04 siap: TRIG=GPIO%d ECHO=GPIO%d", trig_gpio, echo_gpio);

    // [diag SEMENTARA] Hold TRIG HIGH 2 detik sekali di boot -- pulsa normal
    // cuma 10us, kecepetan buat diukur multimeter. Ini buat verifikasi manual
    // pakai multimeter di titik TRIG HC-SR04: kalau nggak kebaca ~3.3V pas ini
    // jalan, sinyal TRIG nggak nyampe (putus/salah pin), bukan soal level 3.3V
    // vs 5V. HAPUS blok ini setelah selesai debug.
    ESP_LOGW(TAG, "[diag] hold TRIG HIGH 2 detik, ukur sekarang pakai multimeter di pin TRIG HC-SR04...");
    gpio_set_level(trig_gpio, 1);
    vTaskDelay(pdMS_TO_TICKS(2000));
    gpio_set_level(trig_gpio, 0);
    ESP_LOGW(TAG, "[diag] TRIG balik LOW");

    // [diag SEMENTARA] Tembak pulsa TRIG cepat (20x/detik) selama 5 detik.
    // Pulsa echo asli terlalu cepat buat ditangkap multimeter biasa, tapi
    // kalau sensor masih hidup & merespons, rata-rata tegangan ECHO bakal
    // sedikit naik dari 0V (bukan diem mulus di 0.00V). Ukur sekarang di pin
    // ECHO HC-SR04 (sisi sensor, SEBELUM voltage divider R4/R5).
    // HAPUS blok ini setelah selesai debug.
    ESP_LOGW(TAG, "[diag] nembak TRIG cepat 20x/detik selama 5 detik, ukur pin ECHO HC-SR04 (sebelum divider) sekarang...");
    for (int i = 0; i < 100; i++) {
        gpio_set_level(trig_gpio, 0);
        esp_rom_delay_us(2);
        gpio_set_level(trig_gpio, 1);
        esp_rom_delay_us(10);
        gpio_set_level(trig_gpio, 0);
        vTaskDelay(pdMS_TO_TICKS(50));
    }
    ESP_LOGW(TAG, "[diag] selesai nembak cepat");

    return ESP_OK;
}

// Tunggu sampai level pin ECHO == `level`, return microdetik yang dihabiskan menunggu,
// atau -1 kalau timeout terlampaui.
static inline int64_t wait_for_echo_level(int level, int64_t timeout_us)
{
    int64_t start = esp_timer_get_time();
    while (gpio_get_level(s_echo_gpio) != level) {
        if (esp_timer_get_time() - start > timeout_us) {
            return -1;
        }
    }
    return esp_timer_get_time() - start;
}

esp_err_t sensor_ultrasonic_read_distance_cm(float *distance_cm)
{
    if (s_trig_gpio == GPIO_NUM_NC || s_echo_gpio == GPIO_NUM_NC || distance_cm == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // [diag] level ECHO mentah sebelum TRIG ditembak, buat bedain "diam total"
    // (open circuit) vs "nyangkut HIGH" (short/floating) vs pulsa yang cuma
    // ketiadaan pantulan biasa.
    int echo_idle_level = gpio_get_level(s_echo_gpio);
    ESP_LOGI(TAG, "[diag] ECHO idle sebelum trigger: %d", echo_idle_level);

    gpio_set_level(s_trig_gpio, 0);
    esp_rom_delay_us(2);
    gpio_set_level(s_trig_gpio, 1);
    esp_rom_delay_us(10);
    gpio_set_level(s_trig_gpio, 0);

    if (wait_for_echo_level(1, ECHO_START_TIMEOUT_US) < 0) {
        ESP_LOGI(TAG, "[diag] timeout nunggu echo mulai (nggak ada pantulan?), ECHO masih di level %d",
                 gpio_get_level(s_echo_gpio));
        return ESP_ERR_TIMEOUT;
    }

    int64_t pulse_us = wait_for_echo_level(0, ECHO_PULSE_TIMEOUT_US);
    if (pulse_us < 0) {
        ESP_LOGD(TAG, "timeout nunggu echo selesai");
        return ESP_ERR_TIMEOUT;
    }

    *distance_cm = pulse_us / US_TO_CM_DIVISOR;
    return ESP_OK;
}

float sensor_ultrasonic_distance_to_level_pct(float distance_cm)
{
    float empty_cm = CONFIG_WATER_TANK_EMPTY_DISTANCE_CM;
    float full_cm = CONFIG_WATER_TANK_FULL_DISTANCE_CM;

    if (empty_cm <= full_cm) {
        // Kalibrasi Kconfig nggak masuk akal (empty harus > full, sensor makin
        // deket ke air pas tangki makin penuh), nggak bisa dihitung.
        return 0.0f;
    }

    float level_pct = (empty_cm - distance_cm) / (empty_cm - full_cm) * 100.0f;
    if (level_pct < 0.0f) {
        level_pct = 0.0f;
    } else if (level_pct > 100.0f) {
        level_pct = 100.0f;
    }
    return level_pct;
}
