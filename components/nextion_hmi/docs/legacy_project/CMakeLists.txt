idf_component_register(
    SRCS
        "main.c"
        "config.c"
        "nextion_transport.c"
        "nextion_events.c"
        "nextion_file_reader.c"
        "nextion_storage.c"
        "nextion_ui.c"
        "heating_program_graph.c"
        "heating_program_models.c"
        "heating_program_validation.c"
    INCLUDE_DIRS "."
)
