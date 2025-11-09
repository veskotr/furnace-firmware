idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES logger-component esp_driver_gpio spi-master-component)