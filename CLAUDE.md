# Konteks Project

Baca [`Architecture.MD`](./Architecture.MD) dan [`README.md`](./README.md) dulu buat
konteks hardware, arsitektur konektivitas (WiFi APSTA), dan kontrak data JSON.
Dokumen ini cuma nambahin rencana fitur yang belum diimplementasi.

# Rencana Fitur: WiFi Provisioning dari Dashboard

## Latar belakang

Ini datang dari diskusi di project sebelah, `my-iot-dashboard` (Next.js, dashboard
web/mobile buat firmware ini). User connect ke hotspot ESP32 (AP, `192.168.4.1`)
buat setup awal, terus mau bisa milih & connect-kan ESP32 ke WiFi rumah/sekitar
**langsung dari dashboard** — tanpa reflash firmware, tanpa buka `idf.py menuconfig`.

Ini juga udah kecatet sebagai item terbuka di `Architecture.MD` bagian
"Belum Diputuskan / Perlu Dicek": *"WiFi provisioning (ganti WiFi tanpa reflash
firmware) — untuk nanti, versi awal masih hardcode via Kconfig"*.

## Status sekarang (udah dicek langsung ke kode, per komit ini)

- `components/wifi_manager/wifi_manager.c` — SSID/password STA cuma diambil dari
  `CONFIG_WIFI_STA_SSID` / `CONFIG_WIFI_STA_PASSWORD` (Kconfig, compile-time),
  langsung di-`esp_wifi_set_config(WIFI_IF_STA, ...)` sekali pas `wifi_manager_init()`.
  **Nggak ada NVS, nggak ada API buat ganti kredensial saat runtime.**
- `components/local_webserver/local_webserver.c` — cuma 2 route:
  `GET /` (HTML dashboard) dan `GET /api/sensors`. **Nggak ada endpoint scan WiFi
  atau endpoint buat kirim kredensial baru.** CORS header (`Access-Control-Allow-Origin: *`)
  cuma ada di `/api/sensors`, belum di route lain.
- AP hotspot ESP32 selalu aktif dari boot (`WIFI_MODE_APSTA`), jadi endpoint baru
  di bawah ini otomatis bisa diakses device yang connect ke hotspot, sama seperti
  `/api/sensors` sekarang.

## Yang perlu ditambah (firmware, project ini)

1. **`wifi_manager`: simpan kredensial ke NVS, bukan cuma Kconfig**
   - Pas `wifi_manager_init()`: coba baca SSID/password dari NVS dulu (namespace
     misal `"wifi_cfg"`); kalau kosong/belum pernah diisi, fallback ke
     `CONFIG_WIFI_STA_SSID`/`_PASSWORD` (Kconfig) kayak sekarang — supaya nggak
     breaking buat yang belum pernah provisioning.
   - Fungsi baru `bool wifi_manager_set_sta_credentials(const char *ssid, const char *password)`:
     - Tulis ke NVS (`nvs_set_str` + `nvs_commit`).
     - `esp_wifi_disconnect()` → update `wifi_config_t` STA baru →
       `esp_wifi_set_config(WIFI_IF_STA, ...)` → `esp_wifi_connect()`.
     - Nggak perlu reboot device; event handler yang udah ada
       (`WIFI_EVENT_STA_DISCONNECTED` → auto-retry, `IP_EVENT_STA_GOT_IP` → update
       `s_sta_connected`) otomatis nanganin status baru, dashboard tinggal polling
       `GET /api/sensors` atau endpoint status buat lihat udah connect apa belum.
   - Fungsi baru (opsional tapi berguna) `bool wifi_manager_get_sta_status(char *ssid_out, size_t len)`
     buat dashboard nampilin lagi nyambung ke SSID apa.

2. **`local_webserver`: dua endpoint baru**
   - `GET /api/wifi/scan` — trigger `esp_wifi_scan_start(NULL, true)` (blocking,
     ini aman dilakuin walopun STA lagi connect — ESP32 bisa scan+connect
     bareng di mode APSTA), abis itu `esp_wifi_scan_get_ap_records()` buat ambil
     hasil, balikin JSON array `[{"ssid":"...","rssi":-55,"secure":true}, ...]`.
     Perhatiin: scan butuh beberapa detik, jangan bikin `httpd` task keblok kelamaan
     kalau bisa dijalanin di task terpisah + client polling hasil, atau minimal
     kasih timeout wajar di sisi handler.
   - `POST /api/wifi/connect` — body JSON `{"ssid": "...", "password": "..."}`,
     panggil `wifi_manager_set_sta_credentials()`, langsung balikin `202 Accepted`
     (jangan nunggu hasil connect di handler yang sama — connect-nya async lewat
     event handler yang udah ada). Dashboard cek status belakangan lewat endpoint
     status atau `/api/sensors` (field `timestamp`/koneksi).
   - Tambahin `Access-Control-Allow-Origin: *` di semua route baru ini juga
     (dashboard yang manggil biasanya beda origin — localhost:3000 pas dev, atau
     domain Vercel pas production — meskipun requestnya ke IP lokal `192.168.4.1`).
   - `POST` butuh baca body request — pakai `httpd_req_recv()` sampai `req->content_len`,
     jangan lupa validasi ukurannya (SSID max 32 byte, password max 64 byte sesuai
     batas WiFi asli) sebelum di-`strlcpy` ke buffer tetap, biar nggak overflow dari
     input yang salah/jahat.

## Yang di luar scope project ini (dikerjain di `my-iot-dashboard`)

- Halaman "Setup WiFi" di dashboard: muncul kalau dashboard kedeteksi lagi diakses
  lewat hotspot ESP32 (bukan lewat cloud/MQTT), nge-fetch `GET /api/wifi/scan` ke
  `192.168.4.1`, user pilih SSID + isi password, `POST /api/wifi/connect`.
- Toggle mode "Hotspot lokal" vs "Cloud (MQTT/Firebase)" di dashboard — ini murni
  frontend, nggak butuh perubahan apa pun di firmware ini karena `local_webserver`
  & `/api/sensors` udah jalan terus tanpa perlu diubah.

## Constraint yang perlu diinget

- Testing endpoint baru ini butuh hardware asli (ESP32 fisik) buat verifikasi WiFi
  scan & reconnect beneran jalan — nggak bisa ditest murni di simulator/CI.
- WiFi credentials dari NVS itu sensitif (password WiFi orang) — nggak perlu
  dienkripsi buat project hobi kayak gini, tapi jangan di-log ke serial monitor
  polos-polos (`ESP_LOGI` yang nge-print password, kayak yang udah ada sekarang
  buat AP password di `wifi_manager_init()` — jangan ditiru buat kredensial STA
  yang baru).
