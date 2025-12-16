#include "esp_task_wdt.h"
#include "health_monitor_internal.h"
#include "sdkconfig.h"
#include "utils.h"

static const char* TAG = "HEALTH_MONITOR_TASK";

typedef struct
{
    const char* task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} health_monitor_task_args_t;

static const health_monitor_task_args_t health_monitor_task_args = {
    .task_name = CONFIG_HEALTH_MONITOR_TASK_NAME,
    .stack_size = CONFIG_HEALTH_MONITOR_TASK_STACK_SIZE,
    .task_priority = CONFIG_HEALTH_MONITOR_TASK_PRIORITY,
};

static esp_err_t init_health_watchdog(void);

static void health_monitor_task(void* arg)
{
    esp_task_wdt_add(NULL); // registers current task

    health_monitor_ctx_t* ctx = (health_monitor_ctx_t*)arg;

    LOGGER_LOG_INFO(TAG, "Health monitor task started");
    const TickType_t period = pdMS_TO_TICKS(CONFIG_HEALTH_MONITOR_CHECK_INTERVAL_MS);
    TickType_t last_wake = xTaskGetTickCount();

    // Health monitor task implementation
    while (ctx->is_running)
    {
        bool system_healthy = true;
        const TickType_t now = xTaskGetTickCount();

        for (uint8_t i = 0; i < CONFIG_HEARTH_BEAT_COUNT; i++)
        {
            heartbeat_entry_t* hb = &ctx->heartbeat[i];
            const TickType_t silence = now - hb->last_seen_tick;

            if (silence > hb->max_silence_ticks)
            {
                if (hb->miss_count < hb->max_misses)
                    hb->miss_count++;

                if (hb->required && hb->miss_count >= hb->max_misses)
                {
                    hb->state = HB_STATE_FAILED;
                    system_healthy = false;
                }
                else
                {
                    hb->state = HB_STATE_MISSED;
                }
            }
            else
            {
                hb->miss_count = 0;
                hb->state = HB_STATE_OK;
            }
        }

        if (system_healthy)
        {
            esp_task_wdt_reset();
        }

        const uint32_t ticks_to_wait = (last_wake + period) - xTaskGetTickCount();
        if (ticks_to_wait > 0)
            ulTaskNotifyTake(pdTRUE, ticks_to_wait);
        last_wake += period;
    }

    esp_task_wdt_reset();
    esp_task_wdt_delete(NULL); // unregisters current task
    LOGGER_LOG_INFO(TAG, "Health monitor task exiting");
    ctx->task_handle = NULL;
    vTaskDelete(NULL);
}

esp_err_t init_health_monitor_task(health_monitor_ctx_t* ctx)
{
    if (ctx != NULL && ctx->is_running)
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(init_health_watchdog(),
                      "Failed to initialize health watchdog");

    ctx->is_running = true;

    //Create task
    CHECK_ERR_LOG_RET(xTaskCreate(
                          health_monitor_task,
                          health_monitor_task_args.task_name,
                          health_monitor_task_args.stack_size,
                          ctx,
                          health_monitor_task_args.task_priority,
                          &ctx->task_handle) == pdPASS
                      ? ESP_OK
                      : ESP_FAIL,
                      "Failed to create health monitor task");

    if (ctx->task_handle == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Health monitor task handle is NULL after creation");
        ctx->is_running = false;
        return ESP_FAIL;
    }
    LOGGER_LOG_INFO(TAG, "Health monitor task initialized");
    ctx->tasks_initialized = true;

    return ESP_OK;
}

esp_err_t shutdown_health_monitor_task(health_monitor_ctx_t* ctx)
{
    if (ctx == NULL || !ctx->is_running)
    {
        return ESP_OK;
    }

    ctx->is_running = false;
    if (ctx->task_handle != NULL)
    {
        xTaskNotifyGive(ctx->task_handle);
    }
    ctx->tasks_initialized = false;

    LOGGER_LOG_INFO(TAG, "Health monitor task shutdown");

    return ESP_OK;
}


static esp_err_t init_health_watchdog(void)
{
    esp_task_wdt_config_t config = {
        .timeout_ms = 5000, // must cover worst-case health cycle
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic = true,
    };

    esp_err_t err = esp_task_wdt_init(&config);

    if (err == ESP_ERR_INVALID_STATE)
    {
        // Already initialized – OK in idempotent init
        return ESP_OK;
    }

    return err;
}
