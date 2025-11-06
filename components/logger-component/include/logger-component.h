#ifndef LOGGER_COMPONENT_H
#define LOGGER_COMPONENT_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

typedef struct
{
    const char *tag;
    const char *message;
} log_message_t;


void logger_init(void);
void logger_send(const char *tag, const char *message);


#endif
