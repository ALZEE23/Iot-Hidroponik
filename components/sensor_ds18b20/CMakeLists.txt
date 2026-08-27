idf_component_register(SRCS "sensor_ds18b20.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio
                    PRIV_REQUIRES onewire_bus ds18b20)
