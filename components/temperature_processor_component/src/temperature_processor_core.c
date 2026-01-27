#include "temperature_processor_component.h"
#include "logger_component.h"
#include "esp_event.h"
#include "temperature_processor_internal.h"
#include "utils.h"

static const char* TAG = "TEMP_PROCESSOR_CORE";

volatile bool processor_running = false;

// Single global context pointer (internal to component)
static temp_processor_context_t* g_temp_processor_ctx = NULL;

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_temp_processor(void)
{
    if (processor_running)
    {
        return ESP_OK;
    }

    // Allocate context if needed
    if (g_temp_processor_ctx == NULL)
    {
        g_temp_processor_ctx = calloc(1, sizeof(temp_processor_context_t));
        if (g_temp_processor_ctx == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate temperature processor context");
            return ESP_ERR_NO_MEM;
        }
    }

    g_temp_processor_ctx->processor_running = true;

    CHECK_ERR_LOG_CALL_RET(start_temp_processor_task(g_temp_processor_ctx),
                           free(g_temp_processor_ctx),
                           "Failed to start temperature processor task");

    return ESP_OK;
}

esp_err_t shutdown_temp_processor(void)
{
    if (g_temp_processor_ctx == NULL || !g_temp_processor_ctx->processor_running)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(stop_temp_processor_task(g_temp_processor_ctx), "Failed to stop temperature processor task");

    g_temp_processor_ctx->processor_running = false;

    // Free context
    free(g_temp_processor_ctx);
    g_temp_processor_ctx = NULL;

    return ESP_OK;
}
