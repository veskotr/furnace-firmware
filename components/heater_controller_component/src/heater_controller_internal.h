#ifndef HEATER_CONTROLLER_INTERNAL_H
#define HEATER_CONTROLLER_INTERNAL_H

#include "esp_err.h"
#include <stdbool.h>
#include "esp_event.h"
#include "event_registry.h"
#include "furnace_error_types.h"

static const bool HEATER_ON = true;
static const bool HEATER_OFF = false;

typedef enum
{
    HEATER_CONTROLLER_ERROR_GPIO,
} heater_controller_error_t;

typedef struct
{
    // Task handle
    TaskHandle_t task_handle;

    // Current heater state
    volatile bool heater_state;

    // Target power level (0.0 to 1.0)
    float target_power_level;

    // Task running flag
    volatile bool task_running;

    bool initialized;
} heater_controller_context_t;

extern heater_controller_context_t* g_heater_controller_context;

// ----------------------------
// Task management functions
// ----------------------------
esp_err_t init_heater_controller_task(heater_controller_context_t* ctx);
esp_err_t shutdown_heater_controller_task(heater_controller_context_t* ctx);

// ----------------------------
// Hardware control functions
// ----------------------------
esp_err_t init_heater_controller();
esp_err_t set_heater_target_power_level(heater_controller_context_t* ctx, float power_level);
esp_err_t toggle_heater(bool state);
esp_err_t shutdown_heater_controller();

// ----------------------------
// Init events
// ----------------------------
esp_err_t init_events(heater_controller_context_t* ctx);

// ----------------------------
// Event posting functions
// ----------------------------
inline esp_err_t post_heater_controller_error(furnace_error_t error);

esp_err_t post_heater_controller_event(heater_controller_event_t event_type,
                                       void* event_data, size_t event_data_size);


#endif // HEATER_CONTROLLER_INTERNAL_H
