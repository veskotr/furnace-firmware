set(NEXTION_SOURCES "src/nextion_hmi.c")

if(CONFIG_NEXTION_HMI_ENABLED)
    list(APPEND NEXTION_SOURCES
        "src/coordinator/hmi_coordinator.c"
        "src/transport/nextion_transport.c"
        "src/transport/nextion_rx_task.c"
        "src/storage/nextion_storage.c"
        "src/storage/nextion_file_reader.c"
        "src/ui/nextion_ui.c"
        "src/events/nextion_events.c"
        "src/events/nextion_parse_utils.c"
        "src/events/nextion_program_handlers.c"
        "src/events/nextion_settings_handlers.c"
        "src/events/nextion_run_handlers.c"
        "src/events/nextion_manual_handlers.c"
        "src/program/heating_program_models.c"
        "src/program/heating_program_graph.c")
endif()

idf_component_register(
    SRCS ${NEXTION_SOURCES}
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS
        "src"
        "src/coordinator"
        "src/transport"
        "src/storage"
        "src/ui"
        "src/events"
        "src/program"
    REQUIRES common driver nvs_flash event_manager logger_component heating_program_validation commands_dispatcher heater_controller_component
)
