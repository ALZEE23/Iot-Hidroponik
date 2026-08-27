#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start esp_http_server lokal, aktif terus (bareng WiFi APSTA) baik lewat AP
// hotspot ESP32 maupun IP STA pas connect ke WiFi rumah. Serve dashboard HTML
// di "/" dan snapshot JSON sensor_hub di "/api/sensors".
esp_err_t local_webserver_start(void);

#ifdef __cplusplus
}
#endif
