file(GLOB SRC_FILES "src/*.c")

idf_component_register(SRCS "${SRC_FILES}"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES logger_component common common temperature_monitor_component esp_common
    REQUIRES esp_event)
