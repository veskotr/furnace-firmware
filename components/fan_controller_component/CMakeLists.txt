idf_component_register(
    SRCS "src/fan_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES driver event_manager logger_component
)
