#ifndef EVENT_REGISTRY_H
#define EVENT_REGISTRY_H

#include "esp_event.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// COORDINATOR COMPONENT EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(COORDINATOR_EVENT); // Event base declaration

typedef enum
{
    // RX Events (external -> coordinator)
    COORDINATOR_EVENT_PROFILE_STARTED = 0,
    COORDINATOR_EVENT_PROFILE_PAUSED,
    COORDINATOR_EVENT_PROFILE_RESUMED,
    COORDINATOR_EVENT_PROFILE_STOPPED,
    COORDINATOR_EVENT_NODE_STARTED,
    COORDINATOR_EVENT_NODE_COMPLETED,
    COORDINATOR_EVENT_ERROR_OCCURRED,
} coordinator_event_id_t;

// Event data structures for coordinator
typedef struct
{
    size_t profile_index;
} coordinator_start_profile_data_t;

typedef enum
{
    COORDINATOR_ERROR_NONE = 0,
    COORDINATOR_ERROR_PROFILE_NOT_PAUSED,
    COORDINATOR_ERROR_PROFILE_NOT_RESUMED,
    COORDINATOR_ERROR_PROFILE_NOT_STOPPED,
    COORDINATOR_ERROR_NOT_STARTED,
} coordinator_error_code_t;

typedef struct
{
    coordinator_error_code_t error_code;
    esp_err_t esp_error_code;
} coordinator_error_data_t;

typedef struct
{
    uint32_t profile_index;
    float current_temperature;
    float target_temperature;
    uint32_t elapsed_ms;
    uint32_t total_ms;
} coordinator_status_data_t;

// ============================================================================
// HEATER CONTROLLER COMPONENT EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(HEATER_CONTROLLER_EVENT);

typedef enum
{
    HEATER_CONTROLLER_ERROR_OCCURRED = 0,
    HEATER_CONTROLLER_HEATER_TOGGLED,
    HEATER_CONTROLLER_STATUS_REPORT_RESPONSE
} heater_controller_event_t;

// ============================================================================
// HEALTH MONITOR COMPONENT EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(HEALTH_MONITOR_EVENT);

typedef enum
{
    HEALTH_MONITOR_EVENT_HEARTBEAT = 0,
} health_monitor_event_id_t;

typedef enum
{
    TEMP_MONITOR_EVENT_HEARTBEAT = 0,
    HEATER_CONTROLLER_EVENT_HEARTBEAT,
    COORDINATOR_EVENT_HEARTBEAT,
    TEMP_PROCESSOR_EVENT_HEARTBEAT,
} health_monitor_component_id_t;

// ============================================================================
// TEMPERATURE PROCESSOR EVENTS
// ============================================================================

ESP_EVENT_DECLARE_BASE(TEMP_PROCESSOR_EVENT);

#define PROCESS_TEMPERATURE_EVENT_DATA 0

typedef struct
{
    float average_temperature;
    bool valid;
} temp_processor_data_t;

// ============================================================================
// FURNACE ERROR EVENTS
// ============================================================================
ESP_EVENT_DECLARE_BASE(FURNACE_ERROR_EVENT);

#define FURNACE_ERROR_EVENT_ID 0

// ============================================================================
// INITIALIZATION FUNCTION
// ============================================================================

/**
 * @brief Initialize all event bases
 *
 * Must be called after event_manager_init() but before components use events
 *
 * @return ESP_OK on success
 */
esp_err_t event_registry_init(void);

#endif // EVENT_REGISTRY_H