#include <string.h>
#include <stdlib.h>
#include "local_webserver.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "sensor_hub.h"

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
"</div>"
"<div class=\"stale\" id=\"status\"></div>"
"<script>"
"async function refresh(){"
"try{"
"const r = await fetch('/api/sensors');"
"const d = await r.json();"
"for (const k of ['water_temp_c','air_temp_c','air_humidity_pct','ph','tds_ppm']) {"
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

    ESP_LOGI(TAG, "local webserver siap di http://192.168.4.1/ (AP) atau IP STA kalau lagi connect ke WiFi rumah");
    return ESP_OK;
}
