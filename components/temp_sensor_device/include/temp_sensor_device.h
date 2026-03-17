#pragma once

#include "device_manager.h"
#include "esp_err.h"

typedef enum
{
    TEMP_SENSOR_DEVICE_COMMAND_SET_REGISTER_VALUE = 0,
    TEMP_SENSOR_REPAIR_FORM_GOOD_UNIT
} temp_sensor_device_cmd_id_t;

typedef struct temp_sensor_device temp_sensor_device_t;

typedef struct
{
    uint16_t register_address;
    uint16_t value;
} temp_sensor_device_set_register_value_cmd_params_t;

esp_err_t temp_sensor_create(temp_sensor_device_t** device);

esp_err_t temp_sensor_set_device_state(const temp_sensor_device_t* device, device_state_t new_state);

esp_err_t temp_sensor_read_device(const temp_sensor_device_t* device, void* data_out);

esp_err_t temp_sensor_write_device(const temp_sensor_device_t* device, const device_write_cmd_t* cmd);

esp_err_t temp_sensor_destroy(temp_sensor_device_t* device);
