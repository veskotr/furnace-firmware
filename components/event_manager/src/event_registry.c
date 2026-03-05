#include "event_registry.h"
#include "logger_component.h"

static const char *TAG = "EVENT_REGISTRY";

// ============================================================================
// EVENT BASE DEFINITIONS
// ============================================================================

ESP_EVENT_DEFINE_BASE(COORDINATOR_EVENT);
ESP_EVENT_DEFINE_BASE(HEATER_CONTROLLER_EVENT);
ESP_EVENT_DEFINE_BASE(HEALTH_MONITOR_EVENT);
ESP_EVENT_DEFINE_BASE(TEMP_PROCESSOR_EVENT);
ESP_EVENT_DEFINE_BASE(FURNACE_ERROR_EVENT);

// ============================================================================
// INITIALIZATION
// ============================================================================

esp_err_t event_registry_init(void)
{
    LOGGER_LOG_INFO(TAG, "Event registry initialized");
    LOGGER_LOG_DEBUG(TAG, "  - COORDINATOR_EVENT base defined");
    LOGGER_LOG_DEBUG(TAG, "  - HEATER_CONTROLLER_EVENT base defined");
    LOGGER_LOG_DEBUG(TAG, "  - HEALTH_MONITOR_EVENT base defined");
    LOGGER_LOG_DEBUG(TAG, "  - TEMP_PROCESSOR_EVENT base defined");
    LOGGER_LOG_DEBUG(TAG, "  - FURNACE_ERROR_EVENT base defined");
    
    return ESP_OK;
}