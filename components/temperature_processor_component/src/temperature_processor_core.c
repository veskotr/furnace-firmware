#include "temperature_processor_component.h"
#include "logger_component.h"
#include "esp_event.h"
#include "temperature_processor_internal.h"
#include "utils.h"

static const char *TAG = "TEMP_PROCESSOR_CORE";

volatile bool processor_running = false;

// Single global context pointer (internal to component)
static temp_processor_context_t *g_temp_processor_ctx = NULL;

static esp_err_t init_devices(void);
static void destroy_devices(void);

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_temp_processor(uint8_t number_of_temp_sensors)
{
    if (processor_running)
    {
        return ESP_OK;
    }

    // Allocate context if needed
    if (g_temp_processor_ctx == NULL)
    {
        g_temp_processor_ctx = calloc(1, sizeof(temp_processor_context_t));
        if (g_temp_processor_ctx == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to allocate temperature processor context");
            return ESP_ERR_NO_MEM;
        }
    }

    if (g_temp_processor_ctx->exit_semaphore == NULL)
    {
        g_temp_processor_ctx->exit_semaphore = xSemaphoreCreateBinary();
        if (g_temp_processor_ctx->exit_semaphore == NULL)
        {
            LOGGER_LOG_ERROR(TAG, "Failed to create temperature processor exit semaphore");
            free(g_temp_processor_ctx);
            g_temp_processor_ctx = NULL;
            return ESP_ERR_NO_MEM;
        }
    }

    atomic_init(&g_temp_processor_ctx->processor_running, true);
    g_temp_processor_ctx->number_of_temp_sensors = number_of_temp_sensors;

    const esp_err_t devices_err = init_devices();
    if (devices_err != ESP_OK)
    {
        destroy_devices();
        vSemaphoreDelete(g_temp_processor_ctx->exit_semaphore);
        free(g_temp_processor_ctx);
        g_temp_processor_ctx = NULL;
        return devices_err;
    }

    const esp_err_t task_err = start_temp_processor_task(g_temp_processor_ctx);
    if (task_err != ESP_OK)
    {
        destroy_devices();
        vSemaphoreDelete(g_temp_processor_ctx->exit_semaphore);
        free(g_temp_processor_ctx);
        g_temp_processor_ctx = NULL;
        return task_err;
    }

    CHECK_ERR_LOG_CALL_RET(init_temp_processor_events(g_temp_processor_ctx),
                           stop_temp_processor_task(g_temp_processor_ctx),
                           "Failed to initialize temperature processor events");

    return ESP_OK;
}

esp_err_t shutdown_temp_processor(void)
{
    if (g_temp_processor_ctx == NULL ||
        !atomic_load_explicit(&g_temp_processor_ctx->processor_running, memory_order_acquire))
    {
        return ESP_OK;
    }

    CHECK_ERR_LOG_RET(shutdown_temp_processor_events(g_temp_processor_ctx),
                      "Failed to shutdown temperature processor events");

    CHECK_ERR_LOG_RET(stop_temp_processor_task(g_temp_processor_ctx), "Failed to stop temperature processor task");

    if (xSemaphoreTake(g_temp_processor_ctx->exit_semaphore, portMAX_DELAY) != pdTRUE)
    {
        return ESP_ERR_TIMEOUT;
    }

    g_temp_processor_ctx->task_handle = NULL;
    atomic_store_explicit(&g_temp_processor_ctx->processor_running, false, memory_order_release);

    vSemaphoreDelete(g_temp_processor_ctx->exit_semaphore);
    free(g_temp_processor_ctx);
    g_temp_processor_ctx = NULL;

    return ESP_OK;
}

static esp_err_t init_devices(void)
{
    if (g_temp_processor_ctx == NULL)
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (uint8_t i = 0; i < g_temp_processor_ctx->number_of_temp_sensors; i++)
    {
        CHECK_ERR_LOG_RET(temp_sensor_create(&g_temp_processor_ctx->temp_sensor_devices[i]),
                          "Failed to create temp sensor device for processor");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay to let device manager process the new device
        CHECK_ERR_LOG_RET(temp_sensor_set_device_state(g_temp_processor_ctx->temp_sensor_devices[i], DEVICE_STATE_RUNNING),
                          "Failed to set temp sensor device state to running");
        vTaskDelay(pdMS_TO_TICKS(1000)); // Small delay to let device manager process the new device
    }

    LOGGER_LOG_INFO(TAG, "Initialized temp sensor devices");

    return ESP_OK;
}

static void destroy_devices(void)
{
    if (g_temp_processor_ctx == NULL)
    {
        return;
    }

    for (uint8_t i = 0; i < g_temp_processor_ctx->number_of_temp_sensors; i++)
    {
        if (g_temp_processor_ctx->temp_sensor_devices[i] != NULL)
        {
            CHECK_ERR_LOG(temp_sensor_destroy(g_temp_processor_ctx->temp_sensor_devices[i]),
                          "Failed to destroy partially initialized temperature sensor");
            g_temp_processor_ctx->temp_sensor_devices[i] = NULL;
        }
    }
}
