#include "utils.h"
#include "temperature_profile_controller.h"
#include "pid_component.h"
#include "coordinator_component_types.h"
#include "coordinator_component_internal.h"
#include "coordinator_component.h"

static const char* TAG = "COORDINATOR_TASK";

typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} CoordinatorTaskConfig_t;

static const CoordinatorTaskConfig_t coordinator_task_config = {
    .task_name = CONFIG_COORDINATOR_TASK_NAME,
    .stack_size = CONFIG_COORDINATOR_TASK_STACK_SIZE,
    .task_priority = CONFIG_COORDINATOR_TASK_PRIORITY
};

static void heating_profile_task(void* args)
{
    coordinator_ctx_t* ctx = (coordinator_ctx_t*)args;

    LOGGER_LOG_INFO(TAG, "Coordinator task started");

    static uint32_t last_wake_time = 0;
    static uint32_t current_time = 0;
    static uint32_t last_update_duration = 0;

    while (ctx->running)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (!ctx->paused)
        {
            current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            last_update_duration = current_time - last_wake_time;
            ctx->heating_task_state.current_time_elapsed_ms += last_update_duration;
            last_wake_time = current_time;
        }

        const profile_controller_error_t err = get_target_temperature_at_time(
            ctx->heating_task_state.current_time_elapsed_ms,
            &ctx->heating_task_state.target_temperature);
        LOGGER_LOG_INFO(TAG, "Elapsed Time: %d ms, Target Temperature: %.2f C",
                        ctx->heating_task_state.current_time_elapsed_ms,
                        ctx->heating_task_state.target_temperature);
        // TODO: Handle error cases
        if (err != PROFILE_CONTROLLER_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "Failed to get target temperature at time %d ms, error: %d",
                            ctx->heating_task_state.current_time_elapsed_ms,
                            err);
            continue;
        }
        // Calculate power output based on current and target temperature
        float power_output = pid_controller_compute(ctx->current_temperature,
                                                    ctx->heating_task_state.target_temperature,
                                                    last_update_duration);

        // Turn on/off heaters based on power output
        //TODO Fix me
        /*CHECK_ERR_LOG(
            post_heater_controller_event(HEATER_CONTROLLER_SET_POWER_LEVEL, &power_output, sizeof(power_output)),
            "Failed to set heater target power level");*/

        LOGGER_LOG_INFO(TAG, "Coordinator notified. Current Temperature: %.2f C",
                        ctx->current_temperature);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    stop_heating_profile(ctx);
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t start_heating_profile(coordinator_ctx_t *ctx, const size_t profile_index)
{
    if (ctx->task_handle != NULL && ctx->running)
    {
        return ESP_OK;
    }

    if (profile_index >= ctx->num_profiles)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid profile index: %d", profile_index);
        return ESP_ERR_INVALID_ARG;
    }

    ctx->heating_task_state.profile_index = profile_index;
    ctx->heating_task_state.is_active = true;
    ctx->heating_task_state.is_paused = false;
    ctx->heating_task_state.is_completed = false;
    ctx->heating_task_state.current_time_elapsed_ms = 0;
    ctx->heating_task_state.total_time_ms = ctx->heating_profiles[profile_index].first_node
        ->duration_ms;
    ctx->heating_task_state.current_temperature = ctx->current_temperature;
    ctx->heating_task_state.heating_element_on = false;
    ctx->heating_task_state.fan_on = false;

    const temp_profile_config_t temp_profile_config = {
        .heating_profile = &ctx->heating_profiles[profile_index],
        .initial_temperature = ctx->current_temperature
    };

    const profile_controller_error_t err = load_heating_profile(temp_profile_config);

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
                               ctx,
                               coordinator_task_config.task_priority,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->task_handle = NULL,
                           "Failed to create coordinator task");
    ctx->running = true;

    LOGGER_LOG_INFO(TAG, "Coordinator task initialized");

    return ESP_OK;
}

esp_err_t coordinator_pause_heating_profile(coordinator_ctx_t *ctx)
{
    if (!ctx->running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ctx->paused = true;
    ctx->heating_task_state.is_paused = true;
    LOGGER_LOG_INFO(TAG, "Heating profile paused");

    return ESP_OK;
}

esp_err_t coordinator_resume_heating_profile(coordinator_ctx_t *ctx)
{
    if (!ctx->running)
    {
        return ESP_ERR_INVALID_STATE;
    }

    ctx->paused = false;
    ctx->heating_task_state.is_paused = false;

    LOGGER_LOG_INFO(TAG, "Heating profile resumed");

    return ESP_OK;
}

esp_err_t coordinator_get_heating_task_state()
{
    //TODO Post state event
    return ESP_FAIL;
}

esp_err_t coordinator_get_current_heating_profile()
{
    //TODO Post current profile event
    return ESP_FAIL;
}

esp_err_t coordinator_stop_heating_profile(coordinator_ctx_t *ctx)
{
    if (!ctx->running)
    {
        return ESP_OK;
    }

    ctx->running = false;

    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }
    ctx->heating_task_state.profile_index = INVALID_PROFILE_INDEX;
    ctx->heating_task_state.is_paused = false;

    shutdown_profile_controller();

    LOGGER_LOG_INFO(TAG, "Coordinator task shutdown complete");

    return ESP_OK;
}
