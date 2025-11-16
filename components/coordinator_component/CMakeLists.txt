file(GLOB SRC_FILES "src/*.c")

idf_component_register(SRCS "${SRC_FILES}"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES logger_component temperature_monitor_component temperature_processor_component
                    REQUIRES esp_event esp_common core_types)
