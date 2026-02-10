idf_component_register(SRCS "main.c"
        INCLUDE_DIRS "."
        REQUIRES common logger_component event_manager heater_controller_component coordinator_component temperature_monitor_component temperature_processor_component health_monitor nextion_hmi run_indicator)