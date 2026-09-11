# Hidroponik IoT — Architecture Reference

Dokumen ini rangkuman keputusan desain project hidroponik (hardware + firmware).
Dipakai sebagai konteks untuk Claude Code atau siapapun yang lanjutin project ini.

## 1. Hardware

### Power Architecture
```
INPUT 12V DC JACK
      │
      ▼
Fuse/PTC + Reverse Polarity Protection (dioda Schottky, misal 1N5819)
      │
      ├──────────────► +12V rail (reserved untuk pompa/relay di masa depan)
      │
      ▼
12V → 5V (buck converter, misal LM2596)
      │
     +5V rail (buat sensor pH & TDS yang butuh 5V)
      │
      ▼
ESP32 DevKit (USB-C) — disuplai dari +5V ke pin VIN
```

**Catatan penting:** ESP32 yang dipakai adalah **ESP32 DevKit klon (USB-C, chip WROOM-32 klasik)**,
bukan modul mentah. Board ini sudah punya regulator 5V→3.3V built-in (AMS1117), jadi:
- Suplai **+5V** ke pin **VIN** board dev (bukan ke 3V3 — itu output)
- Ambil **+3V3** dari pin output board dev untuk sensor yang butuh 3.3V
- Pin **EN** butuh pull-up 10kΩ ke +3V3 (wajib untuk boot ESP32)
- Chip target: **esp32** (bukan esp32s3/c3), untuk `idf.py set-target`

### Sensor & Pin Mapping

| Sensor | Fungsi | Interface | Pin ESP32 | Catatan |
|---|---|---|---|---|
| DS18B20 | Suhu air (waterproof) | 1-Wire | IO4 | Pull-up 4.7kΩ ke 3.3V |
| DHT22 | Suhu & kelembaban udara | 1-Wire custom | IO5 | Pull-up 10kΩ ke 3.3V |
| pH Module (PH-4502C) | pH air nutrisi | Analog | IO36 (SENSOR_VP / ADC1_CH0) | Butuh 5V VCC, cek datasheet fisik |
| TDS Sensor | Kepekatan nutrisi | Analog | IO39 (SENSOR_VN / ADC1_CH3) | Butuh 5V VCC |
| HC-SR04 | Level air (jarak ke permukaan) | GPIO digital (trig/echo) | TRIG=IO32, ECHO=IO33 | VCC 5V. ECHO output 5V wajib lewat voltage divider (R4 1k + R5 2k) sebelum masuk IO33, karena ESP32 cuma toleran 3.3V |

Strapping pins yang **dihindari**: IO0, IO2, IO12, IO15 (dipakai buat boot mode ESP32).

### Firmware Stack
- **ESP-IDF v6.x** (bukan Arduino framework)
- Breaking change penting: driver ADC lama (`driver/adc.h`) dihapus di v6.0 →
  wajib pakai driver baru `esp_adc` (oneshot mode) untuk baca pH & TDS.

## 2. Konektivitas

ESP32 jalan mode **WiFi APSTA (STA + AP bareng, bukan exclusive fallback)**:

```
                    ESP32 Boot
                         │
                         ▼
        WIFI_MODE_APSTA nyala bareng dari awal
                         │
          ┌──────────────┴──────────────┐
          ▼                              ▼
     AP (Hotspot)                   STA (WiFi rumah)
     selalu aktif                   connect + auto-retry
          │                         terus di background
          ▼                              │
   Local HTTP server              ┌───────┴────────┐
   (dashboard, diakses           STA connected     STA belum/putus
   lewat hotspot ESP32               │                   │
   ATAU lewat IP STA kalau            ▼                   ▼
   HP/laptop di WiFi yang sama)  MQTT publish      cuma dashboard lokal
                                  ke HiveMQ Cloud   yang jalan, MQTT/Firebase
                                  + HTTPS PUT ke    auto-lanjut begitu STA
                                  Firebase RTDB     connect lagi
```

- **MQTT → HiveMQ Cloud** (free tier): real-time push data sensor, aktif kapan pun
  STA lagi connect (dicek live tiap siklus baca sensor, bukan cuma status pas boot).
- **Firebase Realtime Database**: nyimpen histori data via HTTPS REST API, sama
  kayak MQTT cuma jalan kalau STA connect. Rules dibuat terbuka (`.read`/`.write`: true)
  — cukup untuk project hobi, bukan production.
- **AP (hotspot) selalu aktif**, bukan cuma fallback — dashboard lokal (`local_webserver`)
  bisa diakses kapan pun lewat hotspot ESP32 (`http://192.168.4.1/`), atau lewat IP STA-nya
  kalau device yang lihat kebetulan di WiFi rumah yang sama (lebih rendah latensi,
  nggak lewat round-trip cloud).
- STA nggak pernah "nyerah" — kalau gagal/putus, terus retry connect di background;
  MQTT (auto-reconnect bawaan esp-mqtt) & push Firebase otomatis aktif lagi begitu
  STA berhasil connect, tanpa perlu reboot device.
- WiFi credentials (SSID/password rumah), HiveMQ Cloud endpoint, dan Firebase
  Database URL disimpan lewat **Kconfig** (`idf.py menuconfig`), bukan hardcode.

### Frontend
- **Next.js** — bisa jadi web (deploy ke Vercel, pakai `mqtt.js` via WSS port 8884
  karena browser nggak bisa buka koneksi MQTT TCP native) atau **Android app via
  Capacitor** (bisa pakai MQTT native, lebih ringan).
- Next.js **tidak perlu** 1 jaringan WiFi yang sama dengan ESP32 selama pakai
  MQTT cloud (HiveMQ) — beda cerita kalau ESP32 lagi di AP/hotspot mode, di
  situ baru device harus connect ke hotspot ESP32 langsung.

## 3. Struktur Firmware (ESP-IDF, component-based)

```
hidroponik-firmware/
├── CMakeLists.txt
├── sdkconfig
├── main/
│   ├── CMakeLists.txt
│   └── main.c                 # orkestrasi: init semua, FreeRTOS tasks
└── components/
    ├── sensor_ds18b20/
    ├── sensor_dht22/
    ├── sensor_ph/              # pakai esp_adc oneshot (bukan driver/adc.h lama)
    ├── sensor_tds/             # pakai esp_adc oneshot
    ├── sensor_ultrasonic/      # HC-SR04, bit-bang trig/echo buat level air
    ├── wifi_manager/           # STA dengan fallback otomatis ke AP mode
    ├── local_webserver/        # esp_http_server, aktif saat AP mode
    ├── mqtt_client_app/        # publish ke HiveMQ Cloud (TLS), aktif saat STA mode
    └── firebase_client/        # HTTPS PUT ke Firebase RTDB, aktif saat STA mode
```

## 4. Status Progress

- [x] Power architecture disepakati & sebagian schematic KiCad selesai (barrel jack,
      fuse, reverse protection, EN pull-up, GND/3V3 net labels)
- [x] Pin mapping 4 sensor ke ESP32 disepakati
- [x] Firebase project + Realtime Database dibuat (rules terbuka)
- [x] Arsitektur konektivitas (STA+fallback AP, MQTT HiveMQ, Firebase RTDB) disepakati
- [x] Setup skeleton project ESP-IDF (CMakeLists, Kconfig, struktur folder)
- [x] Implementasi `wifi_manager` (fondasi, + SNTP time sync buat timestamp histori)
- [x] Implementasi sensor components (`sensor_ds18b20`, `sensor_dht22`, `sensor_ph`, `sensor_tds`)
- [x] Implementasi `mqtt_client_app` & `firebase_client`
- [x] Implementasi `local_webserver` untuk AP mode
- [x] Build & flash ke hardware asli buat verifikasi — DHT22 & sensor pH/TDS (analog)
      udah kebukti baca data beneran di board fisik. DS18B20 belum ketes (belum ada
      unit fisiknya). WiFi STA/MQTT/Firebase belum ketes (kredensial belum diisi).
- [ ] Kalibrasi fisik sensor pH pakai larutan buffer pH 4/7/10 (`PH_NEUTRAL_MV`/`PH_MV_PER_PH` di Kconfig)
- [ ] Kalibrasi/verifikasi sensor TDS (nilai masih fluktuatif, kemungkinan besar cuma
      karena probe belum dicelup ke larutan stabil, bukan bug)
- [ ] Test end-to-end WiFi STA + MQTT (HiveMQ Cloud) + Firebase setelah kredensial diisi
- [ ] Assign footprint & routing PCB di KiCad (belum lanjut dari sisi hardware)

## 5. Belum Diputuskan / Perlu Dicek

- Apakah pH module & TDS module fisik butuh 5V pasti atau ada varian 3.3V-tolerant
  (cek label silkscreen board fisik)
- Rules Firebase mau dikunci lebih ketat atau tetap terbuka jangka panjang
- WiFi provisioning (ganti WiFi tanpa reflash firmware) — untuk nanti, versi awal
  masih hardcode via Kconfig