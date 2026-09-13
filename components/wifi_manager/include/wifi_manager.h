#pragma once

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Nyalain WiFi APSTA (STA + AP bareng, nggak exclusive):
//   - AP (hotspot lokal, CONFIG_WIFI_AP_SSID) selalu aktif dari awal boot,
//     dashboard local_webserver bisa diakses lewat situ kapan pun.
//   - STA nyoba connect ke CONFIG_WIFI_STA_SSID, retry tiap gagal/putus tapi
//     CUMA selama window CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC detik dihitung
//     dari boot. Begitu window abis, STA berhenti nyoba connect sama sekali
//     (radio APSTA jadi cuma ngurusin AP terus-menerus, nggak keganggu retry
//     yang gagal terus -- biar hotspot nggak lag/ilang-ilang pas discan HP).
//     Buat nyoba connect WiFi lagi: reboot/power-cycle device, atau
//     provisioning ulang lewat wifi_manager_set_sta_credentials() (buka
//     window retry baru).
//
// Blocking cuma buat percobaan pertama (CONFIG_WIFI_STA_CONNECT_TIMEOUT_SEC),
// sekadar buat tau status awal pas boot. Return true kalau STA berhasil connect
// dalam timeout itu, false kalau belum.
bool wifi_manager_init(void);

// Status live STA saat ini (bisa berubah kapan aja karena reconnect di background).
// Dipakai buat mutusin apa publish MQTT/Firebase sekarang, bukan cuma snapshot boot.
bool wifi_manager_is_sta_connected(void);

// Ganti kredensial STA saat runtime (dipanggil dari endpoint provisioning dashboard),
// tanpa reboot device. Disimpan ke NVS (namespace "wifi_cfg") supaya tetap kepakai
// abis reboot, lalu langsung esp_wifi_disconnect() -> set_config() -> esp_wifi_connect()
// (async -- fungsi ini nggak nunggu hasil connect, cek statusnya belakangan lewat
// wifi_manager_is_sta_connected() / wifi_manager_get_sta_ssid()).
// Return false kalau ssid kosong/kepanjangan (>32 byte) atau password kepanjangan
// (>64 byte) sesuai batas WiFi asli -- nggak ada apa pun yang ditulis/diubah kalau gagal.
bool wifi_manager_set_sta_credentials(const char *ssid, const char *password);

// Salin SSID STA yang lagi dipakai sekarang (hasil provisioning dari NVS, atau
// fallback CONFIG_WIFI_STA_SSID kalau belum pernah provisioning) ke ssid_out.
// Berguna buat dashboard nampilin status "lagi nyambung ke SSID apa".
void wifi_manager_get_sta_ssid(char *ssid_out, size_t len);

#ifdef __cplusplus
}
#endif
