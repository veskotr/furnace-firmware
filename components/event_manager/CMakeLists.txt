file(GLOB SRC_FILES "src/*.c")

idf_component_register(SRCS "${SRC_FILES}"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_common logger_component common
                    REQUIRES esp_event)
