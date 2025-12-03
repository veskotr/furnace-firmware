file(GLOB SRC_FILES "src/*.c")

idf_component_register(SRCS "${SRC_FILES}"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES driver logger_component esp_common common)
