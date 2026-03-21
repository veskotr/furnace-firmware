#pragma once

#include "esp_err.h"

typedef enum
{
    DEVICE_STATE_UNINITIALIZED = 0,
    DEVICE_STATE_IDLE,
    DEVICE_STATE_RUNNING,
    DEVICE_STATE_ERROR,
    DEVICE_STATE_DISABLED
} device_state_t;

typedef enum
{
    DEVICE_TYPE_UNKNOWN = 0,
    DEVICE_TYPE_TEMP_SENSOR,
    DEVICE_TYPE_HEATER,
    DEVICE_TYPE_RELAY,
    DEVICE_TYPE_DISPLAY,
    DEVICE_TYPE_MODBUS_NODE,
    DEVICE_TYPE_CONTACTOR,
} device_type_t;

typedef struct
{
    uint8_t cmd_id;     // Command identifier (custom per device type)
    void* params;       // Pointer to parameters struct (device-specific)
} device_write_cmd_t;

typedef struct device device_t;

typedef struct
{
    esp_err_t (*init)(void *ctx);
    esp_err_t (*update)(void *ctx);
    esp_err_t (*read)(void *ctx, void* data);
    esp_err_t (*write)(void *ctx, const device_write_cmd_t* cmd);
    esp_err_t (*shutdown)(void *ctx);
} device_ops_t;


esp_err_t device_manager_init(void);

esp_err_t device_manager_create_device(const void* device_ctx, const device_ops_t* ops, const char* name, device_type_t type,
                                       device_t** out_device);

esp_err_t device_manager_read_device(const device_t* device, void* data_out);

esp_err_t device_manager_write_device(const device_t* device, const device_write_cmd_t* cmd);

esp_err_t device_manager_set_device_state(device_t* device, device_state_t new_state);

esp_err_t device_manager_destroy(device_t* device);

esp_err_t device_manager_shutdown(void);
