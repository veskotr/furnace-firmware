file(GLOB SRC_FILES "src/*.c")

idf_component_register(SRCS "${SRC_FILES}"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES logger_component common esp_driver_uart esp_modbus)
