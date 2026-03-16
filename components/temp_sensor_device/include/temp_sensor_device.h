#pragma once

#include "esp_err.h"
#include "device_manager.h"

typedef enum
{
    TEMP_SENSOR_DEVICE_COMMAND_SET_REGISTER_VALUE = 0,
    TEMP_SENSOR_REPAIR_FORM_GOOD_UNIT
} temp_sensor_device_cmd_id_t;

typedef struct
{
    uint16_t register_address;
    uint16_t value;
} temp_sensor_device_set_register_value_cmd_params_t;

esp_err_t temp_sensor_create(device_t** device);

esp_err_t temp_sensor_destroy(device_t* device);
