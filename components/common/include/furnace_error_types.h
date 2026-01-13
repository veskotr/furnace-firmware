//
// Created by vesko on 23.12.2025 г..
//

#ifndef FURNACE_FIRMWARE_FURNACE_ERROR_TYPES_H
#define FURNACE_FIRMWARE_FURNACE_ERROR_TYPES_H
#include "esp_err.h"

typedef enum
{
    SEVERITY_INFO,
    SEVERITY_WARNING,
    SEVERITY_ERROR,
    SEVERITY_CRITICAL
} furnace_error_severity_t;

typedef enum
{
    SOURCE_TEMP_PROCESSOR,
    SOURCE_TEMP_MONITOR,
    SOURCE_HEATER_CONTROLLER,
    SOURCE_COORDINATOR,
    SOURCE_PID_CONTROLLER,
    SOURCE_SPI_MASTER,
    SOURCE_LOGGER,
    SOURCE_UNKNOWN_COMPONENT
} furnace_error_source_t;

typedef struct
{
    furnace_error_severity_t severity;
    furnace_error_source_t source;
    uint32_t error_code;
} furnace_error_t;

#endif //FURNACE_FIRMWARE_FURNACE_ERROR_TYPES_H
