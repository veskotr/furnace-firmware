idf_component_register(SRCS "temperature-monitor-component.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES logger-component spi-master-component
    REQUIRES esp_common esp_event)
