#include "temperature-calculator-component.h"
#include "logger-component.h"
#include "sdkconfig.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_event.h"

// Component tag
static const char *TAG = "TEMP_CALC";

// Task handle
static TaskHandle_t temp_calc_task_handle = NULL;

// Event loop handle
static esp_event_loop_handle_t temp_calc_event_loop = NULL;

// Component running flag
static bool calc_running = false;

// ----------------------------
// Configuration
// ----------------------------
typedef struct {
    const char *task_name;
    uint32_t stack_size;
    UBaseType_t task_priority;
} TempCalcConfig_t;

static const TempCalcConfig_t temp_calc_config = {
    .task_name = "TEMP_CALC_TASK",
    .stack_size = 4096,
    .task_priority = 5
};

// ----------------------------
// Task
// ----------------------------
static void temp_calc_task(void *args)
{
    ESP_LOGI(TAG, "Temperature calculator task started");

    while (calc_running) {
        // TODO: calculate target temperature for current time
        vTaskDelay(pdMS_TO_TICKS(1000)); // example 1s delay
    }

    ESP_LOGI(TAG, "Temperature calculator task exiting");
    vTaskDelete(NULL);
}

// ----------------------------
// Public API
// ----------------------------
esp_err_t init_temp_calculator(esp_event_loop_handle_t loop_handle)
{
    if (calc_running) {
        return ESP_OK;
    }

    temp_calc_event_loop = loop_handle;
    calc_running = true;

    return xTaskCreate(
               temp_calc_task,
               temp_calc_config.task_name,
               temp_calc_config.stack_size,
               NULL,
               temp_calc_config.task_priority,
               &temp_calc_task_handle
           ) == pdPASS ? ESP_OK : ESP_FAIL;
}

esp_err_t shutdown_temp_calculator(void)
{
    if (!calc_running) {
        return ESP_OK;
    }

    calc_running = false;

    if (temp_calc_task_handle) {
        vTaskDelete(temp_calc_task_handle);
        temp_calc_task_handle = NULL;
    }

    return ESP_OK;
}

float calculate_temp_at_time(uint32_t time_ms)
{
    // TODO: implement temperature calculation
    return 0.0f;
}
