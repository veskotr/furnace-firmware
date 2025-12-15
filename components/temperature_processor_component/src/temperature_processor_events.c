#include "temperature_processor_internal.h"
#include "utils.h"
#include "event_manager.h"
#include "event_registry.h"

static const char *TAG = "TEMP_PROCESSOR_EVENTS";

esp_err_t post_temp_processor_event(process_temperature_event_t event_type, void *event_data, size_t event_data_size)
{

    CHECK_ERR_LOG_RET(event_manager_post_blocking(
                          TEMP_PROCESSOR_EVENT,
                          event_type,
                          event_data,
                          event_data_size),
                      "Failed to post temperature processor event");

    return ESP_OK;
}