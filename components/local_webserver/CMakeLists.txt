idf_component_register(SRCS "local_webserver.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_http_server sensor_hub esp_wifi wifi_manager cjson)
