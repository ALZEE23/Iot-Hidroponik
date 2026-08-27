idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES nvs_flash
                                  wifi_manager
                                  sensor_hub
                                  sensor_ds18b20
                                  sensor_dht22
                                  sensor_ph
                                  sensor_tds
                                  mqtt_client_app
                                  firebase_client
                                  local_webserver
                    INCLUDE_DIRS ".")
