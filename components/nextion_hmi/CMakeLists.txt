idf_component_register(
    SRCS
        "src/nextion_hmi.c"
        "src/program_profile_adapter.c"
        "src/config.c"
        "src/nextion_transport.c"
        "src/nextion_events.c"
        "src/nextion_file_reader.c"
        "src/nextion_storage.c"
        "src/nextion_ui.c"
        "src/program_graph.c"
        "src/program_models.c"
        "src/program_validation.c"
    INCLUDE_DIRS "include"
    REQUIRES common driver nvs_flash event_manager
)
