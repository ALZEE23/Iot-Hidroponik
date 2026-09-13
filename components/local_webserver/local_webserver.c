#include <string.h>
#include <stdlib.h>
#include "local_webserver.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "cJSON.h"
#include "sensor_hub.h"
#include "wifi_manager.h"

static const char *TAG = "local_webserver";

static const char INDEX_HTML[] =
"<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
"<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
"<title>Hidroponik Monitor</title>"
"<style>"
"body{font-family:sans-serif;background:#0f172a;color:#e2e8f0;margin:0;padding:24px}"
"h1{font-size:1.3rem;margin-bottom:1rem}"
".grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(160px,1fr));gap:12px}"
".card{background:#1e293b;border-radius:12px;padding:16px}"
".label{font-size:.8rem;color:#94a3b8}"
".value{font-size:1.8rem;font-weight:600;margin-top:4px}"
".stale{color:#f87171;font-size:.75rem;margin-top:8px}"
"</style></head><body>"
"<h1>Hidroponik Monitor</h1>"
"<div class=\"grid\">"
"<div class=\"card\"><div class=\"label\">Suhu Air</div><div class=\"value\" id=\"water_temp_c\">--</div></div>"
"<div class=\"card\"><div class=\"label\">Suhu Udara</div><div class=\"value\" id=\"air_temp_c\">--</div></div>"
"<div class=\"card\"><div class=\"label\">Kelembaban Udara</div><div class=\"value\" id=\"air_humidity_pct\">--</div></div>"
"<div class=\"card\"><div class=\"label\">pH</div><div class=\"value\" id=\"ph\">--</div></div>"
"<div class=\"card\"><div class=\"label\">TDS (ppm)</div><div class=\"value\" id=\"tds_ppm\">--</div></div>"
"<div class=\"card\"><div class=\"label\">Level Air</div><div class=\"value\" id=\"water_level_pct\">--</div></div>"
"</div>"
"<div class=\"stale\" id=\"status\"></div>"
"<script>"
"async function refresh(){"
"try{"
"const r = await fetch('/api/sensors');"
"const d = await r.json();"
"for (const k of ['water_temp_c','air_temp_c','air_humidity_pct','ph','tds_ppm','water_level_pct']) {"
"document.getElementById(k).textContent = (d[k] === null || d[k] === undefined) ? '--' : d[k];"
"}"
"document.getElementById('status').textContent = 'update terakhir: ' + new Date().toLocaleTimeString();"
"}catch(e){"
"document.getElementById('status').textContent = 'gagal ambil data: ' + e;"
"}"
"}"
"refresh();"
"setInterval(refresh, 3000);"
"</script>"
"</body></html>";

static esp_err_t index_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t api_sensors_get_handler(httpd_req_t *req)
{
    char *json = sensor_hub_get_json();
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return err;
}

// GET /api/wifi/status -- info STA yang lagi dipakai sekarang, buat dashboard
// nampilin "ESP32 lagi konek ke WiFi apa".
static esp_err_t api_wifi_status_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    char ssid[33] = {0};
    wifi_manager_get_sta_ssid(ssid, sizeof(ssid));

    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "sta_connected", wifi_manager_is_sta_connected());
    cJSON_AddStringToObject(root, "sta_ssid", ssid);

    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return err;
}

// GET /api/wifi/scan -- scan blocking (esp_wifi_scan_start(NULL, true)), aman
// dijalanin walopun STA lagi connect (mode APSTA bisa scan+connect bareng).
// Catatan: httpd default cuma 1 worker task, jadi selama scan (~detik) request
// lain (termasuk /api/sensors) ke server ini bakal ke-antri dulu -- diterima
// buat project hobi ini, dashboard tinggal kasih loading state pas nge-fetch ini.
static esp_err_t api_wifi_scan_get_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    esp_err_t scan_err = esp_wifi_scan_start(NULL, true);
    if (scan_err != ESP_OK) {
        ESP_LOGW(TAG, "wifi scan gagal: %s", esp_err_to_name(scan_err));
        return httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
    }

    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) {
        return httpd_resp_send(req, "[]", HTTPD_RESP_USE_STRLEN);
    }

    wifi_ap_record_t *records = calloc(ap_count, sizeof(wifi_ap_record_t));
    if (records == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    esp_wifi_scan_get_ap_records(&ap_count, records);

    cJSON *arr = cJSON_CreateArray();
    for (int i = 0; i < ap_count; i++) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "ssid", (const char *) records[i].ssid);
        cJSON_AddNumberToObject(item, "rssi", records[i].rssi);
        cJSON_AddBoolToObject(item, "secure", records[i].authmode != WIFI_AUTH_OPEN);
        cJSON_AddItemToArray(arr, item);
    }
    free(records);

    char *json = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);
    if (json == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    esp_err_t err = httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
    free(json);
    return err;
}

// POST /api/wifi/connect -- body JSON {"ssid":"...","password":"..."}. Cuma
// nyimpen kredensial & minta wifi_manager reconnect (async), nggak nunggu
// hasil connect di sini -- dashboard cek status belakangan lewat /api/sensors
// atau endpoint status lain.
static esp_err_t api_wifi_connect_post_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

    if (req->content_len <= 0 || req->content_len > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "body kosong/kegedean");
        return ESP_FAIL;
    }

    char *body = malloc(req->content_len + 1);
    if (body == NULL) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    int received = 0;
    while (received < req->content_len) {
        int r = httpd_req_recv(req, body + received, req->content_len - received);
        if (r <= 0) {
            free(body);
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "gagal baca body");
            return ESP_FAIL;
        }
        received += r;
    }
    body[req->content_len] = '\0';

    cJSON *root = cJSON_Parse(body);
    free(body);
    if (root == NULL) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "JSON invalid");
        return ESP_FAIL;
    }

    cJSON *ssid_item = cJSON_GetObjectItemCaseSensitive(root, "ssid");
    cJSON *password_item = cJSON_GetObjectItemCaseSensitive(root, "password");
    const char *ssid = cJSON_IsString(ssid_item) ? ssid_item->valuestring : NULL;
    const char *password = cJSON_IsString(password_item) ? password_item->valuestring : "";

    bool ok = (ssid != NULL) && wifi_manager_set_sta_credentials(ssid, password);
    cJSON_Delete(root);

    if (!ok) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST,
                             "ssid wajib diisi (maks 32 char), password maks 64 char");
        return ESP_FAIL;
    }

    httpd_resp_set_status(req, "202 Accepted");
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"status\":\"connecting\"}", HTTPD_RESP_USE_STRLEN);
}

// OPTIONS /api/wifi/connect -- jawab CORS preflight. Browser/WebView otomatis
// kirim OPTIONS duluan sebelum POST asli karena request-nya pakai header
// "Content-Type: application/json" (bukan simple request). Tanpa handler ini,
// esp_http_server balikin 405 buat preflight-nya, jadi POST asli (bawa
// SSID+password) nggak pernah dikirim browser sama sekali.
static esp_err_t api_wifi_connect_options_handler(httpd_req_t *req)
{
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Methods", "POST, OPTIONS");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Headers", "Content-Type");
    httpd_resp_set_status(req, "204 No Content");
    return httpd_resp_send(req, NULL, 0);
}

esp_err_t local_webserver_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    httpd_handle_t server = NULL;

    esp_err_t err = httpd_start(&server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gagal start httpd: %s", esp_err_to_name(err));
        return err;
    }

    httpd_uri_t index_uri = {
        .uri = "/",
        .method = HTTP_GET,
        .handler = index_get_handler,
    };
    httpd_register_uri_handler(server, &index_uri);

    httpd_uri_t api_uri = {
        .uri = "/api/sensors",
        .method = HTTP_GET,
        .handler = api_sensors_get_handler,
    };
    httpd_register_uri_handler(server, &api_uri);

    httpd_uri_t wifi_status_uri = {
        .uri = "/api/wifi/status",
        .method = HTTP_GET,
        .handler = api_wifi_status_get_handler,
    };
    httpd_register_uri_handler(server, &wifi_status_uri);

    httpd_uri_t wifi_scan_uri = {
        .uri = "/api/wifi/scan",
        .method = HTTP_GET,
        .handler = api_wifi_scan_get_handler,
    };
    httpd_register_uri_handler(server, &wifi_scan_uri);

    httpd_uri_t wifi_connect_uri = {
        .uri = "/api/wifi/connect",
        .method = HTTP_POST,
        .handler = api_wifi_connect_post_handler,
    };
    httpd_register_uri_handler(server, &wifi_connect_uri);

    httpd_uri_t wifi_connect_options_uri = {
        .uri = "/api/wifi/connect",
        .method = HTTP_OPTIONS,
        .handler = api_wifi_connect_options_handler,
    };
    httpd_register_uri_handler(server, &wifi_connect_options_uri);

    ESP_LOGI(TAG, "local webserver siap di http://192.168.4.1/ (AP) atau IP STA kalau lagi connect ke WiFi rumah");
    return ESP_OK;
}
