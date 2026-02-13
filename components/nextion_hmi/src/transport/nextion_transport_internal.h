#pragma once

#include <stddef.h>
#include <stdint.h>

void nextion_uart_init(void);
void nextion_send_cmd(const char *cmd);
void nextion_send_raw(const uint8_t *data, size_t length);

void nextion_uart_lock(void);
void nextion_uart_unlock(void);
