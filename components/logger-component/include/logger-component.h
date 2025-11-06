#ifndef LOGGER_COMPONENT_H
#define LOGGER_COMPONENT_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef enum
{
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG,
    LOG_LEVEL_VERBOSE
} log_level_t;

typedef struct
{
    const char *tag;
    const char *message;
    const log_level_t level;
} log_message_t;


void logger_init(void);
void logger_send(log_level_t log_level, const char *tag, const char *message);


#endif
