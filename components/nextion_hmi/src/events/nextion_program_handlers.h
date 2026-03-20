#pragma once

#include <stdint.h>

// Navigate the programs page (backward-compat simple nav)
void program_handlers_page_prev(void);
void program_handlers_page_next(void);

// Reset program state and navigate to programs page
void program_handlers_nav_to_programs(void);

// Handlers called from the line router
void handle_save_prog(const char *payload);
void handle_show_graph(const char *payload);
void handle_autofill(const char *payload);
void handle_prog_page_data(const char *payload);
void handle_add_prog(void);
void handle_delete_prog(const char *current_name);
void handle_confirm_delete(void);
void handle_prog_back(const char *payload);
void handle_edit_prog(const char *payload);
void handle_program_select(const char *filename);
void handle_prog_field_set(const char *payload);
