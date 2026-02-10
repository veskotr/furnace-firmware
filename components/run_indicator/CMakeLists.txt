idf_component_register(
    SRCS "src/run_indicator.c"
    INCLUDE_DIRS "include"
    REQUIRES driver event_manager logger_component
)
