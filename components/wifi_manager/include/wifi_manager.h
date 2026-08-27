#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WIFI_MANAGER_MODE_STA = 0,  // konek ke WiFi rumah, MQTT+Firebase aktif
    WIFI_MANAGER_MODE_AP,       // fallback hotspot lokal, local_webserver aktif
} wifi_manager_mode_t;

// Blocking: coba connect ke WiFi tersimpan (STA) sampai CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC.
// Kalau berhasil, sekalian sync waktu via SNTP lalu return WIFI_MANAGER_MODE_STA.
// Kalau gagal/timeout, otomatis switch ke SoftAP pakai kredensial fallback dan
// return WIFI_MANAGER_MODE_AP.
wifi_manager_mode_t wifi_manager_init(void);

#ifdef __cplusplus
}
#endif
