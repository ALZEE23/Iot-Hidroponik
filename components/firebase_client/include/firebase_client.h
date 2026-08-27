#pragma once

#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

// Push satu snapshot data sensor (JSON) ke Firebase Realtime Database via HTTPS REST:
//   PUT <CONFIG_FIREBASE_DATABASE_URL>/<CONFIG_FIREBASE_PATH_PREFIX>/latest.json
//   PUT <CONFIG_FIREBASE_DATABASE_URL>/<CONFIG_FIREBASE_PATH_PREFIX>/history/<ts>.json
// `ts` dipakai sebagai key histori (unix time, dari SNTP). Blocking selama request HTTP.
esp_err_t firebase_client_push(const char *json_payload, time_t ts);

#ifdef __cplusplus
}
#endif
