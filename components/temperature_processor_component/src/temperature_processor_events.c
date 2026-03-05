#include "temperature_processor_internal.h"
#include "utils.h"
#include "event_manager.h"
#include "event_registry.h"

static const char* TAG = "TEMP_PROCESSOR_EVENTS";

esp_err_t post_temp_processor_event(temp_processor_data_t data)
{
    CHECK_ERR_LOG_RET(event_manager_post_blocking(
                          TEMP_PROCESSOR_EVENT,
                          PROCESS_TEMPERATURE_EVENT_DATA,
                          &data,
                          sizeof(temp_processor_data_t)),
                      "Failed to post temperature processor event");

    return ESP_OK;
}

esp_err_t post_processing_error(furnace_error_t furnace_error)
{
    CHECK_ERR_LOG_RET(event_manager_post_blocking(FURNACE_ERROR_EVENT,
                          FURNACE_ERROR_EVENT_ID,
                          &furnace_error,
                          sizeof(furnace_error_t)),
                      "Failed to post temperature processing error event");

    return ESP_OK;
}
