//
// Created by vesko on 10.3.2026 г..
//
#pragma once

#include <stdbool.h>

#include "esp_err.h"
#include "device_manager.h"

struct temp_sensor_device
{
    uint16_t id;
    float last_temperature;
    bool valid;
    bool allocated;
    uint16_t modbus_address;
    uint16_t modbus_register;
    device_t * device_handle;
};
