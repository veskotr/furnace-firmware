#include "heater_controller_component.h"
#include "heater_controller_internal.h"
#include "utils.h"

static const char* TAG = "HEATER_CTRL_CORE";

heater_controller_context_t* g_heater_controller_context;

esp_err_t heater_controller_set_sensor_data_inhibit(const bool inhibited)
{
    if (g_heater_controller_context == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return heater_controller_set_sensor_data_inhibit_for_context(g_heater_controller_context, inhibited);
}

esp_err_t heater_controller_set_control_inhibit(const bool inhibited)
{
    if (g_heater_controller_context == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }
    return heater_controller_set_control_inhibit_for_context(g_heater_controller_context, inhibited);
}

bool heater_controller_output_is_inhibited(void)
{
    return g_heater_controller_context != NULL &&
           heater_output_is_inhibited(g_heater_controller_context);
}

esp_err_t init_heater_controller_component(void)
{
    if (g_heater_controller_context != NULL && g_heater_controller_context->initialized)
    {
        return ESP_OK;
    }

    // Allocate context if needed
    if (g_heater_controller_context == NULL)
    {
        g_heater_controller_context = calloc(1, sizeof(heater_controller_context_t));
        if (g_heater_controller_context == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate heater controller context");
            return ESP_ERR_NO_MEM;
        }
    }

    g_heater_controller_context->power_mutex = xSemaphoreCreateMutex();
    if (g_heater_controller_context->power_mutex == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create heater controller power mutex");
        shutdown_heater_controller_component();
        return ESP_ERR_NO_MEM;
    }

    g_heater_controller_context->exit_semaphore = xSemaphoreCreateBinary();
    if (g_heater_controller_context->exit_semaphore == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create heater controller exit semaphore");
        shutdown_heater_controller_component();
        return ESP_ERR_NO_MEM;
    }

    CHECK_ERR_LOG_CALL_RET(init_events(g_heater_controller_context),
                           shutdown_heater_controller_component(),
                           "Failed to initialize heater controller events");

    CHECK_ERR_LOG_CALL_RET(init_heater_controller(g_heater_controller_context),
                           shutdown_heater_controller_component(),
                           "Failed to initialize heater controller");

    CHECK_ERR_LOG_CALL_RET(init_heater_controller_task(g_heater_controller_context),
                           shutdown_heater_controller_component(),
                           "Failed to initialize heater controller task");

    g_heater_controller_context->initialized = true;

    return ESP_OK;
}

esp_err_t shutdown_heater_controller_component(void)
{
    if (g_heater_controller_context == NULL || !g_heater_controller_context->initialized)
    {
        return ESP_OK;
    }
    CHECK_ERR_LOG_RET(shutdown_heater_controller_task(g_heater_controller_context),
                      "Failed to shutdown heater controller task");

    CHECK_ERR_LOG_RET(shutdown_heater_controller(g_heater_controller_context), "Failed to shutdown heater controller");

    if (g_heater_controller_context->power_mutex != NULL)
    {
        vSemaphoreDelete(g_heater_controller_context->power_mutex);
        g_heater_controller_context->power_mutex = NULL;
    }

    if (g_heater_controller_context->exit_semaphore != NULL)
    {
        vSemaphoreDelete(g_heater_controller_context->exit_semaphore);
        g_heater_controller_context->exit_semaphore = NULL;
    }

    free(g_heater_controller_context);
    g_heater_controller_context = NULL;


    return ESP_OK;
}
