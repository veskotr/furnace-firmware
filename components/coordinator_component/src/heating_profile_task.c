#include "heating_profile_task.h"
#include "coordinator_component_log.h"
#include "utils.h"
#include "temperature_profile_controller.h"
#include "pid_component.h"
#include "heater_controller_component.h"
#include "coordinator_component_types.h"

static const char *TAG = COORDINATOR_COMPONENT_LOG;

TaskHandle_t coordinator_task_handle = NULL;

static heating_task_state_t heating_task_state = {0};

volatile bool profile_paused = false;

typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} CoordinatorTaskConfig_t;

bool coordinator_running = false;

static void heating_profile_task(void *args)
{
    LOGGER_LOG_INFO(TAG, "Coordinator task started");

    static uint32_t last_wake_time = 0;
    static uint32_t current_time = 0;
    static uint32_t last_update_duration = 0;

    while (coordinator_running)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!profile_paused)
        {
            current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            last_update_duration = current_time - last_wake_time;
            heating_task_state.current_time_elapsed_ms += last_update_duration;
            last_wake_time = current_time;
        }

        profile_controller_error_t err = get_target_temperature_at_time(heating_task_state.current_time_elapsed_ms, &heating_task_state.target_temperature);
        LOGGER_LOG_INFO(TAG, "Elapsed Time: %d ms, Target Temperature: %.2f C",
                        heating_task_state.current_time_elapsed_ms,
                        heating_task_state.target_temperature);
        // TODO: Handle error cases
        if (err != PROFILE_CONTROLLER_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "Failed to get target temperature at time %d ms, error: %d",
                            heating_task_state.current_time_elapsed_ms,
                            err);
            continue;
        }
        // Calculate power output based on current and target temperature
        float power_output = pid_controller_compute(heating_task_state.target_temperature, coordinator_current_temperature, last_update_duration);

        // Turn on/off heaters based on power output
        CHECK_ERR_LOG(set_heater_target_power_level(power_output),
                      "Failed to set heater target power level");

        LOGGER_LOG_INFO(TAG, "Coordinator notified. Current Temperature: %.2f C", coordinator_current_temperature);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    stop_heating_profile();
}

static const CoordinatorTaskConfig_t coordinator_task_config = {
    .task_name = "COORDINATOR_TASK",
    .stack_size = 8192,
    .task_priority = 5};

esp_err_t start_heating_profile(const size_t profile_index)
{
    if (coordinator_task_handle != NULL && coordinator_running)
    {
        return ESP_OK;
    }

    if (profile_index < 0 || profile_index >= number_of_profiles)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid profile index: %d", profile_index);
        return ESP_ERR_INVALID_ARG;
    }

    heating_task_state.profile_index = profile_index;
    heating_task_state.is_active = true;
    heating_task_state.is_paused = false;
    heating_task_state.is_completed = false;
    heating_task_state.current_time_elapsed_ms = 0;
    heating_task_state.total_time_ms = coordinator_heating_profiles[profile_index].first_node->duration_ms;
    heating_task_state.current_temperature = coordinator_current_temperature;
    heating_task_state.heating_element_on = false;
    heating_task_state.fan_on = false;

    profile_controller_error_t err = load_heating_profile(&coordinator_heating_profiles[profile_index], coordinator_current_temperature);

    if (err != PROFILE_CONTROLLER_ERROR_NONE)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to load heating profile index %d, error: %d",
                         profile_index,
                         err);
        return ESP_FAIL;
    }

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               heating_profile_task,
                               coordinator_task_config.task_name,
                               coordinator_task_config.stack_size,
                               NULL,
                               coordinator_task_config.task_priority,
                               &coordinator_task_handle) == pdPASS
                               ? ESP_OK
                               : ESP_FAIL,
                           coordinator_task_handle = NULL,
                           "Failed to create coordinator task");
    coordinator_running = true;

    LOGGER_LOG_INFO(TAG, "Coordinator task initialized");

    return ESP_OK;
}

esp_err_t pause_heating_profile(void)
{
    if (!coordinator_running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    profile_paused = true;
    heating_task_state.is_paused = true;

    LOGGER_LOG_INFO(TAG, "Heating profile paused");

    return ESP_OK;
}

esp_err_t resume_heating_profile(void)
{
    if (!coordinator_running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    profile_paused = false;
    heating_task_state.is_paused = false;

    LOGGER_LOG_INFO(TAG, "Heating profile resumed");

    return ESP_OK;
}

void get_heating_task_state(heating_task_state_t *state_out)
{
    if (state_out == NULL)
    {
        return;
    }

    *state_out = heating_task_state;
}

void get_current_heating_profile(size_t *profile_index_out)
{
    if (profile_index_out == NULL)
    {
        return;
    }

    *profile_index_out = heating_task_state.profile_index;
}

esp_err_t stop_heating_profile(void)
{
    if (!coordinator_running)
    {
        return ESP_OK;
    }

    coordinator_running = false;

    if (coordinator_task_handle)
    {
        vTaskDelete(coordinator_task_handle);
        coordinator_task_handle = NULL;
    }
    heating_task_state.profile_index = -1;
    profile_paused = false;

    shutdown_profile_controller();

    LOGGER_LOG_INFO(TAG, "Coordinator task shutdown complete");

    return ESP_OK;
}