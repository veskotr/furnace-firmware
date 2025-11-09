idf_component_register(SRCS "spi-master-component.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver logger-component
    REQUIRES esp_common)
