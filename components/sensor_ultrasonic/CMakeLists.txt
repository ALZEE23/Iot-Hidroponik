idf_component_register(SRCS "sensor_ultrasonic.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_driver_gpio
                    PRIV_REQUIRES esp_timer freertos)
