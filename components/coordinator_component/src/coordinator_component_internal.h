#ifndef COORDINATOR_COMPONENT_INTERNAL_H
#define COORDINATOR_COMPONENT_INTERNAL_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "core_types.h"
#include "coordinator_component_types.h"
#include "event_registry.h"

const size_t INVALID_PROFILE_INDEX = 0xFFFFFFFF;

typedef struct
{
    TaskHandle_t task_handle;

    heating_profile_t* heating_profiles;
    size_t num_profiles;

    bool running;
    bool paused;
    float current_temperature;

    heating_task_state_t heating_task_state;

    bool events_initialized;
} coordinator_ctx_t;

esp_err_t init_coordinator_events(coordinator_ctx_t* ctx);

esp_err_t shutdown_coordinator_events(coordinator_ctx_t* ctx);

esp_err_t send_coordinator_error_event(coordinator_event_id_t event_type, esp_err_t* event_data,
                                       coordinator_error_code_t coordinator_error_code);

esp_err_t send_coordinator_event(coordinator_event_id_t event_type, void* event_data, size_t event_data_size);

esp_err_t start_heating_profile(coordinator_ctx_t* ctx, size_t profile_index);

esp_err_t pause_heating_profile(coordinator_ctx_t* ctx);

esp_err_t resume_heating_profile(coordinator_ctx_t* ctx);

void get_heating_task_state(const coordinator_ctx_t* ctx, heating_task_state_t* state_out);

void get_current_heating_profile(const coordinator_ctx_t* ctx, size_t* profile_index_out);

esp_err_t stop_heating_profile(coordinator_ctx_t* ctx);

#endif // COORDINATOR_COMPONENT_INTERNAL_H
