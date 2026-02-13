file(GLOB COMMON_SRC_FILES "src/*.c")

idf_component_register(
    SRCS "${COMMON_SRC_FILES}"
    PRIV_REQUIRES esp_common logger_component
    REQUIRES esp_event
    INCLUDE_DIRS "include")
