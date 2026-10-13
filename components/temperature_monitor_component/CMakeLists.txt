file(GLOB SRC_FILES "src/*.c")

# Legacy SPI/MAX31865 implementation. It remains for historical reference but
# is intentionally not a dependency of the production application.
idf_component_register(SRCS "${SRC_FILES}"
    INCLUDE_DIRS "include"
    PRIV_REQUIRES logger_component spi_master_component common esp_common event_manager error_manager)
