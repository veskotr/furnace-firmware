#include "heater_controller_internal.h"
#include "utils.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

void heater_controller_task(void* args)
{
    heater_controller_context_t* ctx = (heater_controller_context_t*)args;
    LOGGER_LOG_INFO(TAG, "Heater Controller Task started");

    static const uint32_t heater_window_ms = CONFIG_HEATER_WINDOW_SIZE_MS;

    while (ctx->task_running)
    {
        const uint32_t on_time = (uint32_t)(ctx->target_power_level * heater_window_ms);
        const uint32_t off_time = heater_window_ms - on_time;

        if (on_time > 0)
        {
            const esp_err_t err = toggle_heater( HEATER_ON);
            if (err != ESP_OK)
            {
                LOGGER_LOG_ERROR(TAG, "Failed to turn heater ON");
                CHECK_ERR_LOG(post_heater_controller_error(HEATER_CONTROLLER_ERR_GPIO),
                              "Failed to post heater controller error event");
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(on_time));
        }

        if (off_time > 0)
        {
            const esp_err_t err = toggle_heater( HEATER_OFF);
            if (err != ESP_OK)
            {
                LOGGER_LOG_ERROR(TAG, "Failed to turn heater OFF");
                CHECK_ERR_LOG(
                    post_heater_controller_event(HEATER_CONTROLLER_ERROR_OCCURRED, HEATER_CONTROLLER_ERR_GPIO, sizeof(
                        heater_controller_error_t)), "Failed to post heater controller error event");
            }
            ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(off_time));
        }
    }

    toggle_heater( HEATER_OFF); // Ensure heater is turned off on exit

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
                               NULL,
                               heater_controller_config.task_priority,
                               &ctx->task_handle) == pdPASS
                           ? ESP_OK
                           : ESP_FAIL,
                           ctx->task_handle = NULL,
                           "Failed to create heater controller task");

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

    shutdown_heater_controller(ctx);

    LOGGER_LOG_INFO(TAG, "Heater Controller Task shut down");

    return ESP_OK;
}

esp_err_t set_heater_target_power_level(heater_controller_context_t* ctx, float power_level)
{
    if (power_level < 0.0f || power_level > 1.0f)
    {
        return ESP_ERR_INVALID_ARG;
    }

    ctx->target_power_level = power_level;

    return ESP_OK;
}
