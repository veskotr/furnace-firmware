#include "temperature-controller-component.h"

esp_event_loop_handle_t temperature_event_loop_handle;

static const char *TAG = "TEMP";
static TaskHandle_t temp_mesure_task_handle = NULL;
static TaskHandle_t temp_set_task_handle = NULL;
static esp_event_loop_handle_t temp_mesure_event_loop = NULL;
static esp_event_loop_handle_t temp_set_event_loop = NULL;
static bool mesurement_running = false;
static bool set_running = false;

static heating_program_t current_heating_program;

static const char *TEMP_MESURE_TASK_NAME = "TEMP_MESURE_TASK";
static const uint32_t TEMP_MESURE_STACK_SIZE = 4096;
static const UBaseType_t TEMP_MESURE_TASK_PRIORITY = 5;

static const char *TEMP_SET_TASK_NAME = "TEMP_SET_TASK";
static const uint32_t TEMP_SET_STACK_SIZE = 4096;
static const UBaseType_t TEMP_SET_TASK_PRIORITY = 5;

static void temp_mesure_task(void *args)
{
}

static void temp_set_task(void *args)
{
}

esp_err_t init_temp_mesure_controller(esp_event_loop_handle_t loop_handle)
{
    if (mesurement_running)
        return ESP_OK;
    temp_mesure_event_loop = loop_handle;

    mesurement_running = true;

    return xTaskCreate(temp_mesure_task, TEMP_MESURE_TASK_NAME, TEMP_MESURE_STACK_SIZE, NULL, TEMP_MESURE_TASK_PRIORITY, &temp_mesure_task_handle) == pdPASS
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t init_temp_set_controller(esp_event_loop_handle_t loop_handle, heating_program_t heating_program)
{
    if (set_running)
        return ESP_OK;
    temp_set_event_loop = loop_handle;
    current_heating_program = heating_program;

    set_running = true;

    return xTaskCreate(temp_set_task, TEMP_SET_TASK_NAME, TEMP_SET_STACK_SIZE, NULL, TEMP_SET_TASK_PRIORITY, &temp_set_task_handle) == pdPASS
               ? ESP_OK
               : ESP_FAIL;
}

esp_err_t shutdown_temp_mesure_controller()
{
    if (!mesurement_running)
        return ESP_OK;
    mesurement_running = false;
    vTaskDelete(temp_mesure_task_handle);
    temp_mesure_task_handle = NULL;
    return ESP_OK;
}

esp_err_t shutdown_temp_set_controller()
{
    if (!set_running)
        return ESP_OK;
    set_running = false;
    vTaskDelete(temp_set_task_handle);
    temp_set_task_handle = NULL;
    return ESP_OK;
}

float read_temp_sensor_data()
{
    return 0.0f;
}

float calculate_set_temp_at_time(uint32_t time_ms)
{
    return 0.0f;
}
