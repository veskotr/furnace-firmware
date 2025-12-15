#include "heater_controller_internal.h"
#include "utils.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "HEATER_CTRL_TASK";

static TaskHandle_t heater_controller_task_handle = NULL;
bool heater_controller_task_running = false;
SemaphoreHandle_t heater_controller_mutex = NULL;

typedef struct
{
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} HeaterControllerConfig_t;

static const HeaterControllerConfig_t heater_controller_config = {
    .task_name = "HEATER_CTRL_TASK",
    .stack_size = 4096,
    .task_priority = 5};

void heater_controller_task(void *pvParameters)
{
    LOGGER_LOG_INFO(TAG, "Heater Controller Task started");

    static const uint32_t heater_window_ms = CONFIG_HEATER_WINDOW_SIZE_MS;

    while (heater_controller_task_running)
    {
        xSemaphoreTake(heater_controller_mutex, portMAX_DELAY);
        uint32_t on_time = (uint32_t)(heater_target_power_level * heater_window_ms);
        uint32_t off_time = heater_window_ms - on_time;
        xSemaphoreGive(heater_controller_mutex);

        if (on_time > 0)
        {
            esp_err_t err = toggle_heater(HEATER_ON);
            if (err != ESP_OK)
            {
                LOGGER_LOG_ERROR(TAG, "Failed to turn heater ON");
                CHECK_ERR_LOG(post_heater_controller_error(HEATER_CONTROLLER_ERR_GPIO), "Failed to post heater controller error event");
            }
            vTaskDelay(pdMS_TO_TICKS(on_time));
        }

        if (off_time > 0)
        {
            esp_err_t err = toggle_heater(HEATER_OFF);
            if (err != ESP_OK)
            {
                LOGGER_LOG_ERROR(TAG, "Failed to turn heater OFF");
                CHECK_ERR_LOG(post_heater_controller_event(HEATER_CONTROLLER_ERROR_OCCURRED, HEATER_CONTROLLER_ERR_GPIO, sizeof(heater_controller_error_t)), "Failed to post heater controller error event");
            }
            vTaskDelay(pdMS_TO_TICKS(off_time));
        }
    }

    toggle_heater(HEATER_OFF); // Ensure heater is turned off on exit

    LOGGER_LOG_INFO(TAG, "Heater Controller Task exiting");
    vTaskDelete(NULL);
}

esp_err_t init_heater_controller_task(void)
{
    if (heater_controller_task_running)
    {
        return ESP_OK;
    }

    heater_controller_mutex = xSemaphoreCreateMutex();
    if (heater_controller_mutex == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Failed to create heater controller mutex");
        return ESP_FAIL;
    }
    xSemaphoreGive(heater_controller_mutex);

    heater_controller_task_running = true;

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               heater_controller_task,
                               heater_controller_config.task_name,
                               heater_controller_config.stack_size,
                               NULL,
                               heater_controller_config.task_priority,
                               &heater_controller_task_handle) == pdPASS
                               ? ESP_OK
                               : ESP_FAIL,
                           heater_controller_task_handle = NULL,
                           "Failed to create heater controller task");

    LOGGER_LOG_INFO(TAG, "Heater Controller Task initialized");

    return ESP_OK;
}

esp_err_t shutdown_heater_controller_task(void)
{
    if (!heater_controller_task_running)
    {
        return ESP_OK;
    }
    heater_controller_task_running = false;
    vTaskDelete(heater_controller_task_handle);
    heater_controller_task_handle = NULL;

    if (heater_controller_mutex != NULL)
    {
        vSemaphoreDelete(heater_controller_mutex);
        heater_controller_mutex = NULL;
    }

    shutdown_heater_controller();

    LOGGER_LOG_INFO(TAG, "Heater Controller Task shut down");

    return ESP_OK;
}