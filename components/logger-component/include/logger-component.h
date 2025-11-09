#ifndef LOGGER_COMPONENT_H
#define LOGGER_COMPONENT_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include <stdarg.h>

typedef enum
{
    LOG_LEVEL_INFO,
    LOG_LEVEL_WARN,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE
} log_level_t;

typedef struct
{
    char *tag;
    char message[CONFIG_LOG_MAX_MESSAGE_LENGTH];
    log_level_t level;
} log_message_t;

void logger_init(void);
void logger_send(log_level_t log_level, const char *tag, const char *message, ...);

#define logger_send_info(tag, fmt, ...) logger_send(LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__)
#define logger_send_warn(tag, fmt, ...) logger_send(LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__)
#define logger_send_error(tag, fmt, ...) logger_send(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__)
#define logger_send_debug(tag, fmt, ...) logger_send(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__)
#define logger_send_verbose(tag, fmt, ...) logger_send(LOG_LEVEL_VERBOSE, tag, fmt, ##__VA_ARGS__)

#endif
