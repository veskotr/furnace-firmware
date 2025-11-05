idf_component_register(SRCS "temperature-controller-component.c"
                    INCLUDE_DIRS "include"
                    REQUIRES esp_common esp_event core tinyexpr logger-component)
