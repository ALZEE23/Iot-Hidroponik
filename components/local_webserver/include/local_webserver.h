#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Start esp_http_server lokal (dipakai pas AP mode). Serve dashboard HTML di "/"
// dan snapshot JSON sensor_hub di "/api/sensors".
esp_err_t local_webserver_start(void);

#ifdef __cplusplus
}
#endif
