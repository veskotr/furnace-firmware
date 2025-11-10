idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES logger_component esp_driver_gpio spi_master_component)