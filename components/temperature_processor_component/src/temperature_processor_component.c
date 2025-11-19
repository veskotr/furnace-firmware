#include "temperature_processor_component.h"
#include "logger_component.h"
#include "esp_event.h"
#include "temperature_processor_task.h"
#include "utils.h"
#include "temperature_processor_log.h"

static const char *TAG = TEMP_PROCESSOR_LOG_TAG;

volatile bool processor_running = false;

ESP_EVENT_DEFINE_BASE(PROCESS_TEMPERATURE_EVENT);


// Event loop handle
esp_event_loop_handle_t temp_processor_event_loop = NULL;

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_temp_processor(esp_event_loop_handle_t loop_handle)
{
    if (processor_running)
    {
        return ESP_OK;
    }

    temp_processor_event_loop = loop_handle;
    processor_running = true;

    CHECK_ERR_LOG_RET(start_temp_processor_task(), "Failed to start temperature processor task");

    return ESP_OK;
}

esp_err_t shutdown_temp_processor(void)
{
    if (!processor_running)
    {
        return ESP_OK;
    }

    processor_running = false;

    CHECK_ERR_LOG_RET(stop_temp_processor_task(), "Failed to stop temperature processor task");

    return ESP_OK;
}