#include <string.h>
#include "wifi_manager.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "nvs.h"

static const char *TAG = "wifi_manager";

#define STA_CONNECTED_BIT BIT0

#define STA_SSID_MAX_LEN 32
#define STA_PASSWORD_MAX_LEN 64

#define WIFI_CFG_NVS_NAMESPACE "wifi_cfg"
#define NVS_KEY_STA_SSID "ssid"
#define NVS_KEY_STA_PASSWORD "pass"

#define STA_RECONNECT_DELAY_MS 5000

static EventGroupHandle_t s_wifi_event_group;
static volatile bool s_sta_connected;
static bool s_sntp_started;
static char s_sta_ssid[STA_SSID_MAX_LEN + 1];
static esp_timer_handle_t s_reconnect_timer;

// Baca kredensial STA dari NVS (udah pernah provisioning lewat dashboard);
// fallback ke Kconfig (CONFIG_WIFI_STA_SSID/_PASSWORD) kalau NVS kosong/belum
// pernah diisi, supaya device yang belum pernah provisioning tetap jalan sama
// persis kayak sebelum fitur ini ada.
static void load_sta_credentials(char *ssid_out, size_t ssid_len, char *password_out, size_t password_len)
{
    strlcpy(ssid_out, CONFIG_WIFI_STA_SSID, ssid_len);
    strlcpy(password_out, CONFIG_WIFI_STA_PASSWORD, password_len);

    nvs_handle_t handle;
    if (nvs_open(WIFI_CFG_NVS_NAMESPACE, NVS_READONLY, &handle) != ESP_OK) {
        return;
    }

    char stored_ssid[STA_SSID_MAX_LEN + 1] = {0};
    size_t len = sizeof(stored_ssid);
    if (nvs_get_str(handle, NVS_KEY_STA_SSID, stored_ssid, &len) == ESP_OK && strlen(stored_ssid) > 0) {
        strlcpy(ssid_out, stored_ssid, ssid_len);

        char stored_password[STA_PASSWORD_MAX_LEN + 1] = {0};
        size_t plen = sizeof(stored_password);
        if (nvs_get_str(handle, NVS_KEY_STA_PASSWORD, stored_password, &plen) == ESP_OK) {
            strlcpy(password_out, stored_password, password_len);
        } else {
            password_out[0] = '\0';
        }
    }

    nvs_close(handle);
}

static void sync_time_via_sntp(void)
{
    if (s_sntp_started) {
        return; // sekali init aja, esp_netif_sntp otomatis re-sync periodik sendiri
    }
    s_sntp_started = true;

    ESP_LOGI(TAG, "sync waktu via SNTP (%s)...", CONFIG_WIFI_SNTP_SERVER);
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_WIFI_SNTP_SERVER);
    esp_netif_sntp_init(&config);

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync awal gagal/timeout, bakal ke-sync sendiri di background");
    } else {
        ESP_LOGI(TAG, "waktu berhasil disinkronkan");
    }
}

// STA nggak pernah nyerah -- tiap disconnect, jadwalin percobaan connect lagi
// abis STA_RECONNECT_DELAY_MS (bukan instan) via timer ini, supaya nggak jadi
// tight-loop yang ganggu radio pas ada operasi lain (scan WiFi dari dashboard,
// dll). Kredensial salah/AP nggak ada di jangkauan tetap bakal terus dicoba
// lagi tiap 5 detik selamanya, sampai berhasil atau diprovisioning ulang --
// nggak ada kondisi "berhenti nyoba" permanen lagi kayak sebelumnya.
static void reconnect_timer_callback(void *arg)
{
    esp_wifi_connect();
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        xEventGroupClearBits(s_wifi_event_group, STA_CONNECTED_BIT);
        ESP_LOGW(TAG, "STA disconnect, coba connect lagi dalam %d detik...", STA_RECONNECT_DELAY_MS / 1000);
        esp_timer_stop(s_reconnect_timer); // abaikan error kalau emang lagi nggak jalan
        esp_timer_start_once(s_reconnect_timer, (int64_t) STA_RECONNECT_DELAY_MS * 1000);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "STA got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_sta_connected = true;
        xEventGroupSetBits(s_wifi_event_group, STA_CONNECTED_BIT);
        sync_time_via_sntp();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join AP, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave AP, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

bool wifi_manager_is_sta_connected(void)
{
    return s_sta_connected;
}

bool wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();

    const esp_timer_create_args_t reconnect_timer_args = {
        .callback = &reconnect_timer_callback,
        .name = "wifi_sta_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&reconnect_timer_args, &s_reconnect_timer));

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                          &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                          &wifi_event_handler, NULL, NULL));

    char sta_ssid[STA_SSID_MAX_LEN + 1];
    char sta_password[STA_PASSWORD_MAX_LEN + 1];
    load_sta_credentials(sta_ssid, sizeof(sta_ssid), sta_password, sizeof(sta_password));
    strlcpy(s_sta_ssid, sta_ssid, sizeof(s_sta_ssid));

    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *) sta_config.sta.ssid, sta_ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *) sta_config.sta.password, sta_password, sizeof(sta_config.sta.password));
    if (strlen(sta_password) == 0) {
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    wifi_config_t ap_config = {
        .ap = {
            .channel = CONFIG_WIFI_AP_CHANNEL,
            .max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };
    strlcpy((char *) ap_config.ap.ssid, CONFIG_WIFI_AP_SSID, sizeof(ap_config.ap.ssid));
    ap_config.ap.ssid_len = strlen(CONFIG_WIFI_AP_SSID);
    strlcpy((char *) ap_config.ap.password, CONFIG_WIFI_AP_PASSWORD, sizeof(ap_config.ap.password));
    if (strlen(CONFIG_WIFI_AP_PASSWORD) == 0) {
        ap_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP aktif terus: SSID:%s password:%s channel:%d",
             CONFIG_WIFI_AP_SSID, CONFIG_WIFI_AP_PASSWORD, CONFIG_WIFI_AP_CHANNEL);
    ESP_LOGI(TAG, "nyoba connect STA ke SSID:%s (nggak pernah nyerah, retry tiap %ds kalau gagal/putus)",
             sta_ssid, STA_RECONNECT_DELAY_MS / 1000);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, STA_CONNECTED_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC * 1000));

    return (bits & STA_CONNECTED_BIT) != 0;
}

bool wifi_manager_set_sta_credentials(const char *ssid, const char *password)
{
    if (ssid == NULL || password == NULL) {
        return false;
    }

    size_t ssid_len = strlen(ssid);
    size_t password_len = strlen(password);
    if (ssid_len == 0 || ssid_len > STA_SSID_MAX_LEN || password_len > STA_PASSWORD_MAX_LEN) {
        ESP_LOGW(TAG, "kredensial STA baru ditolak: ssid_len=%d password_len=%d (batas 32/64)",
                 (int) ssid_len, (int) password_len);
        return false;
    }

    nvs_handle_t handle;
    esp_err_t err = nvs_open(WIFI_CFG_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal buka NVS buat simpan kredensial STA: %s", esp_err_to_name(err));
        return false;
    }
    nvs_set_str(handle, NVS_KEY_STA_SSID, ssid);
    nvs_set_str(handle, NVS_KEY_STA_PASSWORD, password);
    err = nvs_commit(handle);
    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal commit NVS kredensial STA: %s", esp_err_to_name(err));
        return false;
    }

    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *) sta_config.sta.ssid, ssid, sizeof(sta_config.sta.ssid));
    strlcpy((char *) sta_config.sta.password, password, sizeof(sta_config.sta.password));
    if (password_len == 0) {
        sta_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    strlcpy(s_sta_ssid, ssid, sizeof(s_sta_ssid));

    // Batalin percobaan reconnect terjadwal (kalau ada) -- kita mau connect
    // pakai kredensial baru ini SEKARANG, bukan nunggu timer lama yang mungkin
    // masih jalan dari kredensial sebelumnya.
    esp_timer_stop(s_reconnect_timer); // abaikan error kalau emang lagi nggak jalan

    // Urutan ini penting: set_config dulu baru disconnect/connect, supaya
    // kalau ada retry yang kepicu (baik dari event handler atau connect()
    // eksplisit di bawah), confignya udah pasti yang baru -- bukan SSID/
    // password lama yang mau diganti.
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_config));
    esp_wifi_disconnect();
    esp_wifi_connect();

    // sengaja nggak nge-log password di sini (beda sama AP password di init) --
    // ini kredensial WiFi rumah orang, jangan nongol polos di serial monitor.
    ESP_LOGI(TAG, "kredensial STA baru disimpan, instruksi reconnect terkirim (ssid:%s)", ssid);
    return true;
}

void wifi_manager_get_sta_ssid(char *ssid_out, size_t len)
{
    strlcpy(ssid_out, s_sta_ssid, len);
}
