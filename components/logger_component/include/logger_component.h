#ifndef LOGGER_COMPONENT_H
#define LOGGER_COMPONENT_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "sdkconfig.h"
#include <stdarg.h>

typedef enum
{
    LOG_LEVEL_NONE = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} log_level_t;

#if CONFIG_LOG_ENABLE
#define LOGGER_LOG_INFO(tag, fmt, ...)  do{ if(CONFIG_LOG_LEVEL >= LOG_LEVEL_INFO) logger_send(LOG_LEVEL_INFO, tag, fmt, ##__VA_ARGS__); }while(0)
#define LOGGER_LOG_WARN(tag, fmt, ...)  do{ if(CONFIG_LOG_LEVEL >= LOG_LEVEL_WARN) logger_send(LOG_LEVEL_WARN, tag, fmt, ##__VA_ARGS__); }while(0)
#define LOGGER_LOG_ERROR(tag, fmt, ...) do{ if(CONFIG_LOG_LEVEL >= LOG_LEVEL_ERROR) logger_send(LOG_LEVEL_ERROR, tag, fmt, ##__VA_ARGS__); }while(0)
#define LOGGER_LOG_DEBUG(tag, fmt, ...) do{ if(CONFIG_LOG_LEVEL >= LOG_LEVEL_DEBUG) logger_send(LOG_LEVEL_DEBUG, tag, fmt, ##__VA_ARGS__); }while(0)
#else
#define LOGGER_LOG_INFO(tag, fmt, ...) ((void)0)
#define LOGGER_LOG_WARN(tag, fmt, ...) ((void)0)
#define LOGGER_LOG_ERROR(tag, fmt, ...) ((void)0)
#define LOGGER_LOG_DEBUG(tag, fmt, ...) ((void)0)
#define LOGGER_LOG_VERBOSE(tag, fmt, ...) ((void)0)
#endif

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

#endif
