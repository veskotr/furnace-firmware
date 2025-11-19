#ifndef HEATER_CONTROLLER_TYPES_H
#define HEATER_CONTROLLER_TYPES_H
#include "esp_event.h"

ESP_EVENT_DECLARE_BASE(HEATER_CONTROLLER_EVENT);

typedef enum
{
    HEATER_CONTROLLER_ERROR_OCCURRED = 0
} heater_controller_event_t;

typedef enum
{
    HEATER_CONTROLLER_ERR_GPIO = 0,
    HEATER_CONTROLLER_ERR_UNKNOWN
} heater_controller_error_t;

#endif // HEATER_CONTROLLER_TYPES_H