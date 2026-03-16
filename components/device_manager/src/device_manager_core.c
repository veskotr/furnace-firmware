//
// Created by vesko on 9.3.2026 г..
//

#include "device_manager.h"
#include "esp_err.h"
#include "device_manager_internal.h"
#include "utils.h"
#include "sdkconfig.h"

static const char* TAG = "DEVICE_MANAGER_CORE";

device_manager_context_t* g_device_manager_context;

esp_err_t device_manager_init(void)
{
    if (g_device_manager_context != NULL && g_device_manager_context->running)
    {
        return ESP_OK;
    }

    // Allocate context if needed
    if (g_device_manager_context == NULL)
    {
        g_device_manager_context = calloc(1, sizeof(device_manager_context_t));
        if (g_device_manager_context == NULL)
        {
            return ESP_ERR_NO_MEM;
        }
    }

    CHECK_ERR_LOG_CALL_RET(init_device_manager_task(g_device_manager_context),
                           device_manager_shutdown(),
                           "Failed to initialize device manager task");

    LOGGER_LOG_INFO(TAG, "Device manager initialized successfully");

    return ESP_OK;
}

esp_err_t device_manager_create_device(const void* device_ctx, const device_ops_t* ops, const char* name,
                                       const device_type_t type,
                                       device_t** out_device)
{
    if (out_device == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }
    if (g_device_manager_context == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    if (g_device_manager_context->count >= CONFIG_DEVICE_MANAGER_MAX_DEVICES)
    {
        return ESP_ERR_NO_MEM;
    }

    printf("Creating device: name=%s, type=%d\n", name, type);
    for (uint8_t i = 0; i < CONFIG_DEVICE_MANAGER_MAX_DEVICES; i++)
    {
        LOGGER_LOG_INFO(TAG, "Checking device slot %d: state=%d", i, g_device_manager_context->devices[i].state);
        if (g_device_manager_context->devices[i].state == DEVICE_STATE_UNINITIALIZED)
        {
            device_t* device = &g_device_manager_context->devices[i];

            LOGGER_LOG_INFO(TAG, "Found free device slot at index %d", i);
            device->type = type;
            device->name = name;
            device->ops = ops;
            device->id = i;
            device->state = DEVICE_STATE_RUNNING;
            device->ctx = (void*)device_ctx;

            *out_device = device;
            CHECK_ERR_LOG_CALL_RET_FMT(device->ops->init(device->ctx),
                                       device_manager_destroy(device),
                                       "Failed to initialize device %s (id %d)", name, i);
            LOGGER_LOG_INFO(TAG, "Device %s created successfully with id: %d", name, i);
            return ESP_OK;
        }
    }
    return ESP_ERR_NO_MEM;
}

esp_err_t device_manager_read_device(const device_t* device, void* data_out)
{
    if (device == NULL || device->state != DEVICE_STATE_RUNNING || device->ops == NULL || device->ops->read == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    CHECK_ERR_LOG_RET_FMT(device->ops->read(device->ctx, data_out),
                          "Failed to read from device %s (id %d)", device->name, device->id);
    return ESP_OK;
}


esp_err_t device_manager_write_device(const device_t* device, const device_write_cmd_t* cmd)
{
    if (device == NULL || device->state != DEVICE_STATE_RUNNING || device->ops == NULL || device->ops->write == NULL)
    {
        return ESP_ERR_INVALID_ARG;
    }

    CHECK_ERR_LOG_RET_FMT(device->ops->write(device->ctx, cmd),
                          "Failed to write to device %s (id %d)", device->name, device->id);

    return ESP_OK;
}

esp_err_t device_manager_set_device_state(device_t* device, device_state_t new_state)
{
    if (device == NULL || device->state == DEVICE_STATE_UNINITIALIZED)
    {
        return ESP_ERR_INVALID_ARG;
    }

    device->state = new_state;
    return ESP_OK;
}

esp_err_t device_manager_destroy(device_t* device)
{
    if (device == NULL || device->state == DEVICE_STATE_UNINITIALIZED)
    {
        return ESP_ERR_INVALID_ARG;
    }

    device->state = DEVICE_STATE_UNINITIALIZED;
    device->ops = NULL;

    return ESP_OK;
}


esp_err_t device_manager_shutdown(void)
{
    if (g_device_manager_context == NULL || !g_device_manager_context->running)
    {
        return ESP_OK;
    }
    CHECK_ERR_LOG_RET(stop_device_manager_task(g_device_manager_context),
                      "Failed to stop device manager task");

    LOGGER_LOG_INFO(TAG, "Device manager task stopped successfully");

    return ESP_OK;
}
