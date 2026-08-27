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
#define STA_FAIL_BIT       BIT1

static EventGroupHandle_t s_wifi_event_group;
static int s_retry_count = 0;
#define STA_MAX_RETRY 3

static void sta_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_count < STA_MAX_RETRY) {
            esp_wifi_connect();
            s_retry_count++;
            ESP_LOGI(TAG, "retry connect STA (%d/%d)", s_retry_count, STA_MAX_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, STA_FAIL_BIT);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *) event_data;
        ESP_LOGI(TAG, "STA got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_count = 0;
        xEventGroupSetBits(s_wifi_event_group, STA_CONNECTED_BIT);
    }
}

static void ap_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == WIFI_EVENT_AP_STACONNECTED) {
        wifi_event_ap_staconnected_t *event = (wifi_event_ap_staconnected_t *) event_data;
        ESP_LOGI(TAG, "station " MACSTR " join, AID=%d", MAC2STR(event->mac), event->aid);
    } else if (event_id == WIFI_EVENT_AP_STADISCONNECTED) {
        wifi_event_ap_stadisconnected_t *event = (wifi_event_ap_stadisconnected_t *) event_data;
        ESP_LOGI(TAG, "station " MACSTR " leave, AID=%d, reason=%d",
                 MAC2STR(event->mac), event->aid, event->reason);
    }
}

static void sync_time_via_sntp(void)
{
    ESP_LOGI(TAG, "sync waktu via SNTP (%s)...", CONFIG_WIFI_SNTP_SERVER);
    esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_WIFI_SNTP_SERVER);
    esp_netif_sntp_init(&config);

    if (esp_netif_sntp_sync_wait(pdMS_TO_TICKS(10000)) != ESP_OK) {
        ESP_LOGW(TAG, "SNTP sync gagal/timeout, timestamp histori bakal ngaco sampai sync berhasil di background");
    } else {
        ESP_LOGI(TAG, "waktu berhasil disinkronkan");
    }
}

static wifi_manager_mode_t try_connect_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    esp_netif_create_default_wifi_sta();

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                          &sta_event_handler, NULL, &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                          &sta_event_handler, NULL, &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    strlcpy((char *) wifi_config.sta.ssid, CONFIG_WIFI_STA_SSID, sizeof(wifi_config.sta.ssid));
    strlcpy((char *) wifi_config.sta.password, CONFIG_WIFI_STA_PASSWORD, sizeof(wifi_config.sta.password));
    if (strlen(CONFIG_WIFI_STA_PASSWORD) == 0) {
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "nyoba connect STA ke SSID:%s (timeout %ds)", CONFIG_WIFI_STA_SSID,
             CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC);

    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                            STA_CONNECTED_BIT | STA_FAIL_BIT,
                                            pdFALSE, pdFALSE,
                                            pdMS_TO_TICKS(CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC * 1000));

    esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, instance_any_id);
    esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, instance_got_ip);

    if (bits & STA_CONNECTED_BIT) {
        return WIFI_MANAGER_MODE_STA;
    }

    ESP_LOGW(TAG, "STA gagal connect, fallback ke AP mode");
    ESP_ERROR_CHECK(esp_wifi_stop());
    return WIFI_MANAGER_MODE_AP;
}

static void start_softap(void)
{
    esp_netif_create_default_wifi_ap();

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                          &ap_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .ap = {
            .channel = CONFIG_WIFI_AP_CHANNEL,
            .max_connection = CONFIG_WIFI_AP_MAX_STA_CONN,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .required = true,
            },
        },
    };
    strlcpy((char *) wifi_config.ap.ssid, CONFIG_WIFI_AP_SSID, sizeof(wifi_config.ap.ssid));
    wifi_config.ap.ssid_len = strlen(CONFIG_WIFI_AP_SSID);
    strlcpy((char *) wifi_config.ap.password, CONFIG_WIFI_AP_PASSWORD, sizeof(wifi_config.ap.password));

    if (strlen(CONFIG_WIFI_AP_PASSWORD) == 0) {
        wifi_config.ap.authmode = WIFI_AUTH_OPEN;
    }

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "AP mode aktif. SSID:%s password:%s channel:%d",
             CONFIG_WIFI_AP_SSID, CONFIG_WIFI_AP_PASSWORD, CONFIG_WIFI_AP_CHANNEL);
}

wifi_manager_mode_t wifi_manager_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_manager_mode_t mode = try_connect_sta();

    if (mode == WIFI_MANAGER_MODE_STA) {
        sync_time_via_sntp();
    } else {
        start_softap();
    }

    return mode;
}
