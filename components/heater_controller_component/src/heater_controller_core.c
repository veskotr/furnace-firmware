#include "heater_controller_component.h"
#include "heater_controller_internal.h"
#include "utils.h"

static const char* TAG = "HEATER_CTRL_CORE";

heater_controller_context_t* g_heater_controller_context;

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

    CHECK_ERR_LOG_RET(init_events(g_heater_controller_context), "Failed to initialize heater controller events");

    CHECK_ERR_LOG_RET(init_heater_controller(g_heater_controller_context), "Failed to initialize heater controller");

    CHECK_ERR_LOG_RET(init_heater_controller_task(g_heater_controller_context),
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

    free(g_heater_controller_context);
    g_heater_controller_context = NULL;

    return ESP_OK;
}
