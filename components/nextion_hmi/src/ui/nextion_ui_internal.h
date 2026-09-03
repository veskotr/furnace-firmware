#pragma once

#include <stdbool.h>

void nextion_show_error(const char *message);
void nextion_clear_error(void);

/* Success overlay — same components as the error overlay but with a
 * "Success" header. Auto-dismisses the loading dialog if one is showing. */
void nextion_show_success(const char *message);

/* Loading dialog — reuses the confirm dialog background (confirmBdy) and
 * text (confirmTxt) with NO buttons, so the user cannot dismiss it from the
 * panel. While it is active the line router ignores all Nextion input.
 * Always pair a nextion_show_loading() with a nextion_show_error() /
 * nextion_show_success() (both auto-hide it) or an explicit
 * nextion_hide_loading(), otherwise the UI stays blocked. */
void nextion_show_loading(const char *message);
void nextion_hide_loading(void);
bool nextion_is_loading(void);
