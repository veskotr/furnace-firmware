#include "coordinator_component_task.h"
#include "coordinator_component_log.h"
#include "utils.h"
#include "temperature_profile_controller.h"
#include "pid_component.h"
#include "heater_controller_component.h"

static const char *TAG = COORDINATOR_COMPONENT_LOG;

TaskHandle_t coordinator_task_handle = NULL;

volatile bool profile_paused = false;

typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} CoordinatorTaskConfig_t;

bool coordinator_running = false;

static void coordinator_task(void *args)
{
    LOGGER_LOG_INFO(TAG, "Coordinator task started");

    static uint32_t elapsed_time = 0;
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
            elapsed_time += last_update_duration;
            last_wake_time = current_time;
        }
        float target_temperature;

        profile_controller_error_t err = get_target_temperature_at_time(elapsed_time, &target_temperature);
        LOGGER_LOG_INFO(TAG, "Elapsed Time: %d ms, Target Temperature: %.2f C",
                        elapsed_time,
                        target_temperature);
        //TODO: Handle error cases
        if (err != PROFILE_CONTROLLER_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "Failed to get target temperature at time %d ms, error: %d",
                            elapsed_time,
                            err);
            continue;
        }
        // Calculate power output based on current and target temperature
        float power_output = pid_controller_compute(coordinator_current_temperature, target_temperature, last_update_duration);

        // Turn on/off heaters based on power output
        set_heater_target_power_level(power_output);

        LOGGER_LOG_INFO(TAG, "Coordinator notified. Current Temperature: %.2f C", coordinator_current_temperature);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    shutdown_coordinator_task();
}

static const CoordinatorTaskConfig_t coordinator_task_config = {
    .task_name = "COORDINATOR_TASK",
    .stack_size = 8192,
    .task_priority = 5};

esp_err_t init_coordinator_task()
{
    if (coordinator_task_handle != NULL && coordinator_running)
    {
        return ESP_OK;
    }

    LOGGER_LOG_INFO(TAG, "Coordinator task initialized");

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               coordinator_task,
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

    return ESP_OK;
}

esp_err_t shutdown_coordinator_task(void)
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

    LOGGER_LOG_INFO(TAG, "Coordinator task shutdown complete");

    return ESP_OK;
}