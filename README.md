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

## Catatan

- Sensor TDS pakai formula polynomial standar (DFRobot Gravity TDS) dengan kompensasi
  suhu dari pembacaan DS18B20.
- TLS ke HiveMQ Cloud & Firebase pakai certificate bundle bawaan ESP-IDF
  (`esp_crt_bundle_attach`), nggak perlu embed PEM manual.
- SoftAP (`WIFI_AP_SSID` / `WIFI_AP_PASSWORD`) selalu aktif dari boot, dashboard bisa
  diakses di `http://192.168.4.1/` kapan pun. STA nyoba connect ke WiFi rumah bareng
  AP (WiFi APSTA) dan terus retry di background kalau gagal/putus — nggak pernah nyerah,
  nggak perlu reboot manual. MQTT & push Firebase otomatis aktif begitu STA connect.
