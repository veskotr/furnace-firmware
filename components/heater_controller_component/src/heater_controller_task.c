#include "heater_controller_internal.h"
#include "utils.h"
#include "sdkconfig.h"
#include "event_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>

static const char* TAG = "HEATER_CTRL_TASK";

typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} HeaterControllerConfig_t;

static const HeaterControllerConfig_t heater_controller_config = {
    .task_name = CONFIG_HEATER_CONTROLLER_TASK_NAME,
    .stack_size = CONFIG_HEATER_CONTROLLER_TASK_STACK_SIZE,
    .task_priority = CONFIG_HEATER_CONTROLLER_TASK_PRIORITY,
};

static void check_error_and_post_event(heater_controller_context_t* ctx, esp_err_t err);

static float get_heater_target_power_level(const heater_controller_context_t* ctx);

static const health_monitor_data_t heater_health_data = {
    .component_id = CONFIG_HEATER_CONTROLLER_COMPONENT_ID,
    .component_name = "Heater Controller",
    .timeout_ticks = pdMS_TO_TICKS(CONFIG_HEATER_CONTROLLER_HEARTBEAT_TIMEOUT_MS)
};

void heater_controller_task(void* args)
{
    heater_controller_context_t* ctx = (heater_controller_context_t*)args;
    LOGGER_LOG_INFO(TAG, "Heater Controller Task started");

    static const uint32_t heater_window_ms = CONFIG_HEATER_WINDOW_SIZE_MS;

    while (ctx->task_running)
    {
        const uint32_t on_time = (uint32_t)(get_heater_target_power_level(ctx) * heater_window_ms);
        const uint32_t off_time = heater_window_ms - on_time;

        if (on_time > 0 && !heater_output_is_inhibited(ctx))
        {
            const esp_err_t err = toggle_heater(HEATER_ON);
            check_error_and_post_event(ctx, err);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(on_time));
        }

        if (off_time > 0)
        {
            const esp_err_t err = toggle_heater(HEATER_OFF);
            check_error_and_post_event(ctx, err);
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(off_time));
        }

        event_manager_post_health(HEALTH_MONITOR_EVENT_HEARTBEAT, &heater_health_data);
    }

    check_error_and_post_event(ctx, toggle_heater(HEATER_OFF)); // Ensure heater is turned off on exit

    LOGGER_LOG_INFO(TAG, "Heater Controller Task exiting");
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t init_heater_controller_task(heater_controller_context_t* ctx)
{
    if (ctx->task_running)
    {
        return ESP_OK;
    }

    ctx->task_running = true;

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               heater_controller_task,
                               heater_controller_config.task_name,
                               heater_controller_config.stack_size,
                               ctx,
                               heater_controller_config.task_priority,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->task_handle = NULL,
                           "Failed to create heater controller task");

    event_manager_post_health(HEALTH_MONITOR_EVENT_REGISTER, &heater_health_data);

    LOGGER_LOG_INFO(TAG, "Heater Controller Task initialized");

    return ESP_OK;
}

esp_err_t shutdown_heater_controller_task(heater_controller_context_t* ctx)
{
    if (!ctx->task_running)
    {
        return ESP_OK;
    }

    ctx->task_running = false;
    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }
    ctx->task_handle = NULL;

    shutdown_heater_controller();

    LOGGER_LOG_INFO(TAG, "Heater Controller Task shut down");

    return ESP_OK;
}

esp_err_t set_heater_target_power_level(heater_controller_context_t* ctx, const float power_level)
{
    if (!isfinite(power_level))
    {
        LOGGER_LOG_ERROR(TAG, "Rejected non-finite heater power command; clearing target");
        return clear_heater_target_power_level(ctx) == ESP_OK
                   ? ESP_ERR_INVALID_ARG
                   : ESP_FAIL;
    }

    if (power_level < 0.0f || power_level > 1.0f)
    {
        return ESP_ERR_INVALID_ARG;
    }
    xSemaphoreTake(ctx->power_mutex, portMAX_DELAY);
    if (ctx->output_inhibited || ctx->sensor_data_inhibited || ctx->control_inhibited)
    {
        xSemaphoreGive(ctx->power_mutex);
        LOGGER_LOG_ERROR(TAG, "Rejected heater power command while output is inhibited");
        return ESP_ERR_INVALID_STATE;
    }
    ctx->target_power_level = power_level;
    xSemaphoreGive(ctx->power_mutex);

    return ESP_OK;
}

esp_err_t clear_heater_target_power_level(heater_controller_context_t* ctx)
{
    xSemaphoreTake(ctx->power_mutex, portMAX_DELAY);
    ctx->target_power_level = 0.0f;
    xSemaphoreGive(ctx->power_mutex);

    return ESP_OK;
}

bool heater_output_is_inhibited(heater_controller_context_t* ctx)
{
    xSemaphoreTake(ctx->power_mutex, portMAX_DELAY);
    const bool inhibited = ctx->output_inhibited || ctx->sensor_data_inhibited || ctx->control_inhibited;
    xSemaphoreGive(ctx->power_mutex);
    return inhibited;
}

esp_err_t heater_controller_set_sensor_data_inhibit_for_context(heater_controller_context_t* ctx, const bool inhibited)
{
    if (ctx == NULL || ctx->power_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->power_mutex, portMAX_DELAY);
    ctx->sensor_data_inhibited = inhibited;
    if (inhibited)
    {
        ctx->target_power_level = 0.0f;
    }
    xSemaphoreGive(ctx->power_mutex);

    if (inhibited)
    {
        /* Enforce physical off independently of the dispatcher queue. */
        const esp_err_t ssr_err = toggle_heater(HEATER_OFF);
        const esp_err_t contactor_err = stop_heater();
        return ssr_err != ESP_OK ? ssr_err : contactor_err;
    }

    return ESP_OK;
}

esp_err_t heater_controller_set_control_inhibit_for_context(heater_controller_context_t* ctx, const bool inhibited)
{
    if (ctx == NULL || ctx->power_mutex == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(ctx->power_mutex, portMAX_DELAY);
    ctx->control_inhibited = inhibited;
    if (inhibited)
    {
        ctx->target_power_level = 0.0f;
    }
    xSemaphoreGive(ctx->power_mutex);

    if (inhibited)
    {
        /* Enforce physical off independently of the dispatcher queue. */
        const esp_err_t ssr_err = toggle_heater(HEATER_OFF);
        const esp_err_t contactor_err = stop_heater();
        return ssr_err != ESP_OK ? ssr_err : contactor_err;
    }

    return ESP_OK;
}

void heater_controller_handle_ssr_failure(heater_controller_context_t* ctx, const esp_err_t ssr_err)
{
    if (ssr_err == ESP_OK)
    {
        return;
    }

    /* Establish the local safety state before attempting any I/O or telemetry.
     * This is intentionally independent of the command dispatcher and HMI. */
    xSemaphoreTake(ctx->power_mutex, portMAX_DELAY);
    ctx->output_inhibited = true;
    ctx->target_power_level = 0.0f;
    xSemaphoreGive(ctx->power_mutex);

    const esp_err_t retry_err = toggle_heater(HEATER_OFF);
    if (retry_err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "SSR off retry failed after GPIO error: %d", retry_err);
    }

    const esp_err_t contactor_err = stop_heater();
    if (contactor_err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Contactor off failed after SSR GPIO error: %d", contactor_err);
    }

    LOGGER_LOG_ERROR(TAG, "Heater output inhibited after SSR GPIO error: %d", ssr_err);
    const furnace_error_t furnace_err = {
        .severity = SEVERITY_CRITICAL,
        .source = SOURCE_HEATER_CONTROLLER,
        .error_code = HEATER_CONTROLLER_ERROR_GPIO
    };
    CHECK_ERR_LOG(post_heater_controller_error(furnace_err), "Failed to post heater controller error event");
}

static float get_heater_target_power_level(const heater_controller_context_t* ctx)
{
    xSemaphoreTake(ctx->power_mutex, portMAX_DELAY);
    const float power_level = ctx->target_power_level;
    xSemaphoreGive(ctx->power_mutex);

    if (!isfinite(power_level))
    {
        LOGGER_LOG_ERROR(TAG, "Non-finite heater target detected; treating as zero");
        return 0.0f;
    }

    return power_level;
}

static void check_error_and_post_event(heater_controller_context_t* ctx, const esp_err_t err)
{
    if (err != ESP_OK)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to turn heater ON/OFF");
        heater_controller_handle_ssr_failure(ctx, err);
    }
}
