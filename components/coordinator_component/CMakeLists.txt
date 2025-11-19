file(GLOB SRC_FILES "src/*.c")

idf_component_register(SRCS "${SRC_FILES}"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES logger_component temperature_monitor_component temperature_processor_component esp_event esp_common temperature_profile_controller common pid_component heater_controller_component)
