#ifndef COORDINATOR_COMPONENT_TYPES_H
#define COORDINATOR_COMPONENT_TYPES_H

#include <stdbool.h>
#include <inttypes.h>

typedef struct
{
    uint32_t profile_index;
    float current_temperature;
    float target_temperature;
    bool is_active;
    bool is_paused;
    bool is_completed;
    int32_t current_time_elapsed_ms;
    int32_t total_time_ms;
    bool heating_element_on;
    bool fan_on;
} heating_task_state_t;


ESP_EVENT_DECLARE_BASE(COORDINATOR_RX_EVENT);
ESP_EVENT_DECLARE_BASE(COORDINATOR_TX_EVENT);

typedef enum
{
    COORDINATOR_EVENT_START_PROFILE = 0,
    COORDINATOR_EVENT_PAUSE_PROFILE,
    COORDINATOR_EVENT_RESUME_PROFILE,
    COORDINATOR_EVENT_STOP_PROFILE,
    COORDINATOR_EVENT_GET_STATUS_REPORT,
    COORDINATOR_EVENT_GET_CURRENT_PROFILE,
} coordinator_rx_event_t;

typedef enum
{
    COORDINATOR_EVENT_PROFILE_STARTED = 0,
    COORDINATOR_EVENT_PROFILE_PAUSED,
    COORDINATOR_EVENT_PROFILE_RESUMED,
    COORDINATOR_EVENT_PROFILE_STOPPED,
    COORDINATOR_EVENT_NODE_STARTED,
    COORDINATOR_EVENT_NODE_COMPLETED,
    COORDINATOR_EVENT_MEASURE_TEMPERATURE,
    COORDINATOR_EVENT_CALCULATE_TARGET_TEMPERATURE,
    COORDINATOR_EVENT_ERROR_OCCURRED
} coordinator_tx_event_t;

typedef enum {
    COORDINATOR_ERROR_NONE = 0,
    COORDINATOR_ERROR_PROFILE_NOT_PAUSED,
    COORDINATOR_ERROR_PROFILE_NOT_RESUMED,
    COORDINATOR_ERROR_PROFILE_NOT_STOPPED,
    COORDINATOR_ERROR_NOT_STARTED,
} coordinator_error_code_t;

typedef struct {
    esp_err_t esp_error_code;
    coordinator_error_code_t coordinator_error_code;
} coordinator_error_t;

#endif // COORDINATOR_COMPONENT_TYPES_H