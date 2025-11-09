idf_component_register(SRCS "temperature-calculator-component.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES logger-component
    REQUIRES esp_common esp_event)
