idf_component_register(
    SRCS "src/modbus_temp_monitor.c"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES logger_component event_manager common esp_driver_uart esp_event)
