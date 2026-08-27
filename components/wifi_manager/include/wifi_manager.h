#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Nyalain WiFi APSTA (STA + AP bareng, nggak exclusive):
//   - AP (hotspot lokal, CONFIG_WIFI_AP_SSID) selalu aktif dari awal boot,
//     dashboard local_webserver bisa diakses lewat situ kapan pun.
//   - STA nyoba connect ke CONFIG_WIFI_STA_SSID, tetap terus retry di background
//     kalau gagal/putus (nggak pernah nyerah), biar otomatis reconnect begitu
//     WiFi rumah balik lagi -- MQTT/Firebase otomatis lanjut begitu STA connect.
//
// Blocking cuma buat percobaan pertama (CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC),
// sekadar buat tau status awal pas boot. Return true kalau STA berhasil connect
// dalam timeout itu, false kalau belum (tapi tetap lanjut retry di background).
bool wifi_manager_init(void);

// Status live STA saat ini (bisa berubah kapan aja karena reconnect di background).
// Dipakai buat mutusin apa publish MQTT/Firebase sekarang, bukan cuma snapshot boot.
bool wifi_manager_is_sta_connected(void);

#ifdef __cplusplus
}
#endif
