idf_component_register(SRCS "sensor_dht22.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio
                    PRIV_REQUIRES esp_timer)
