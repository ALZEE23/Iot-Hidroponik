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

static const char *TAG = "wifi_manager";

#define STA_CONNECTED_BIT BIT0

static EventGroupHandle_t s_wifi_event_group;
static volatile bool s_sta_connected;
static bool s_sntp_started;

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

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                                int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        s_sta_connected = false;
        xEventGroupClearBits(s_wifi_event_group, STA_CONNECTED_BIT);
        ESP_LOGW(TAG, "STA disconnect, retry connect di background...");
        esp_wifi_connect();
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

    wifi_config_t sta_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *) sta_config.sta.ssid, CONFIG_WIFI_STA_SSID, sizeof(sta_config.sta.ssid));
    strlcpy((char *) sta_config.sta.password, CONFIG_WIFI_STA_PASSWORD, sizeof(sta_config.sta.password));
    if (strlen(CONFIG_WIFI_STA_PASSWORD) == 0) {
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
    ESP_LOGI(TAG, "nyoba connect STA ke SSID:%s (timeout awal %ds, retry terus di background kalau gagal)",
             CONFIG_WIFI_STA_SSID, CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, STA_CONNECTED_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC * 1000));

    return (bits & STA_CONNECTED_BIT) != 0;
}
