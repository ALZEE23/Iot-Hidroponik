# Hidroponik IoT Firmware

Firmware ESP-IDF v6.1 (target `esp32`) buat monitoring hidroponik: baca sensor suhu
air (DS18B20), suhu & kelembaban udara (DHT22), pH, dan TDS nutrisi, lalu kirim
datanya lewat MQTT (HiveMQ Cloud) & Firebase Realtime Database kalau STA connect ke
WiFi, sekaligus serve dashboard web lokal terus-menerus lewat SoftAP-nya sendiri
(WiFi APSTA, dua-duanya jalan bareng, bukan exclusive fallback).

Detail desain hardware, pin mapping, dan arsitektur lengkap ada di
[`Architecture.MD`](./Architecture.MD).

## Struktur

```
main/                   orkestrasi: app_main(), sensor_task, Kconfig
components/
  sensor_hub/           shared data model + JSON schema
  wifi_manager/         WiFi APSTA: AP selalu aktif + STA auto-retry di background, SNTP
  sensor_ds18b20/       suhu air (onewire_bus + espressif/ds18b20)
  sensor_dht22/         suhu & kelembaban udara (bit-bang custom)
  sensor_ph/            pH module (esp_adc oneshot)
  sensor_tds/           TDS module (esp_adc oneshot, temp-compensated)
  mqtt_client_app/      publish JSON ke HiveMQ Cloud (mqtts://, TLS cert bundle)
  firebase_client/      PUT JSON ke Firebase RTDB (HTTPS, TLS cert bundle)
  local_webserver/      dashboard HTML + /api/sensors, aktif terus (AP & STA)
```

## Setup

1. `idf.py set-target esp32` (kalau belum).
2. `idf.py menuconfig` -> isi:
   - **Hidroponik WiFi Configuration**: SSID/password WiFi rumah (STA), kredensial hotspot (AP).
   - **MQTT (HiveMQ Cloud)**: broker host, username, password, topic.
   - **Firebase Realtime Database**: database host.
   - **Sensor Configuration**: pin GPIO (default sesuai `Architecture.MD`), interval baca,
     dan **kalibrasi pH** (`PH_NEUTRAL_MV` / `PH_MV_PER_PH` — wajib dikalibrasi ulang
     pakai larutan buffer pH 4/7/10 sebelum dipakai beneran, nilai default cuma tebakan kasar).
3. `idf.py build flash monitor`.

## Kontrak Data (buat Web/App)

Satu schema JSON yang sama dipakai di 3 tempat (didefinisikan sekali di
`components/sensor_hub/sensor_hub.c`, fungsi `sensor_hub_get_json()`):

```json
{
  "water_temp_c": 26.4,
  "air_temp_c": 29.1,
  "air_humidity_pct": 68.5,
  "ph": 6.2,
  "tds_ppm": 812.3,
  "timestamp": 1798765432
}
```

- Semua field numerik jadi `null` (bukan hilang dari objek) kalau sensor itu gagal
  dibaca di siklus itu — cek `data.ph === null`, jangan andalkan `"ph" in data`.
- `timestamp`: unix time detik (UTC). `0` kalau ESP32 belum sempat SNTP sync
  (baru bisa sync kalau STA lagi/udah pernah connect).

Tiga cara ambil data ini dari web/app:

1. **MQTT (realtime, butuh STA connect)** — subscribe topic `CONFIG_MQTT_TOPIC`
   (default `hidroponik/sensor`) di broker HiveMQ Cloud yang sama. Payload persis
   JSON di atas, dipublish tiap `CONFIG_SENSOR_READ_INTERVAL_SEC` detik. Browser
   pakai `mqtt.js` via WSS port 8884 (bukan port TCP 8883, browser nggak bisa buka
   koneksi TCP mentah).
2. **Firebase Realtime Database (histori, butuh STA connect)** — dari `<Database
   URL>/<CONFIG_FIREBASE_PATH_PREFIX>/latest.json` (snapshot terakhir) atau
   `.../history/<unix_ts>.json` (tiap entri histori, key-nya unix timestamp).
   Payload sama persis, ditulis tiap `CONFIG_FIREBASE_PUSH_INTERVAL_READS` siklus baca.
3. **REST lokal (langsung ke device, jalan kapan pun)** — `GET /api/sensors` ke
   `http://192.168.4.1/` (lewat hotspot ESP32) atau ke IP STA-nya (kalau device
   yang minta ada di WiFi rumah yang sama). Payload sama persis, dihitung on-demand
   tiap request (bukan pre-generate), jadi selalu snapshot terbaru dari `sensor_hub`.

## Catatan

- Sensor TDS pakai formula polynomial standar (DFRobot Gravity TDS) dengan kompensasi
  suhu dari pembacaan DS18B20.
- TLS ke HiveMQ Cloud & Firebase pakai certificate bundle bawaan ESP-IDF
  (`esp_crt_bundle_attach`), nggak perlu embed PEM manual.
- SoftAP (`WIFI_AP_SSID` / `WIFI_AP_PASSWORD`) selalu aktif dari boot, dashboard bisa
  diakses di `http://192.168.4.1/` kapan pun. STA nyoba connect ke WiFi rumah bareng
  AP (WiFi APSTA) dan terus retry di background kalau gagal/putus — nggak pernah nyerah,
  nggak perlu reboot manual. MQTT & push Firebase otomatis aktif begitu STA connect.
