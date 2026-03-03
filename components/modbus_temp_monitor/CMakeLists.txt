if(CONFIG_MODBUS_TEMP_ENABLED)
    set(srcs
        "src/modbus_rtu.c"
        "src/ms9024.c"
        "src/modbus_temp_monitor.c"
    )
else()
    set(srcs "src/modbus_temp_monitor_stub.c")
endif()

idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "src"
    PRIV_REQUIRES logger_component event_manager common esp_driver_uart esp_event)
