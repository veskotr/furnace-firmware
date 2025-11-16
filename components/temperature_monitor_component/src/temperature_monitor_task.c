#include "temperature_monitor_task.h"
#include "temperature_monitor_component.h"
#include "temperature_sensors.h"
#include "temperature_monitor_internal_types.h"
#include "ring_buffer.h"
#include "utils.h"
#include "temperature_monitor_log.h"

#include "esp_event.h"
#include "logger_component.h"

static const char *TAG = TEMP_MONITOR_LOG;

ESP_EVENT_DEFINE_BASE(TEMP_MONITOR_EVENT);
// Task handle
static TaskHandle_t temp_monitor_task_handle;

temp_monitor_t temp_monitor = {0};

static temp_sample_t temp_sample = {0};

static const TempMonitorConfig_t temp_monitor_config = {
    .task_name = "TEMP_MONITOR_TASK",
    .stack_size = 4096,
    .task_priority = 5};

static temp_sensor_fault_type_t classify_sensor_fault(temp_sensor_t *sensor_data);

static esp_err_t post_temp_monitor_error(temp_monitor_error_t type, esp_err_t esp_err_code);

static esp_err_t post_temp_monitor_event(temp_monitor_event_t event_type, void *event_data, size_t event_data_size);

static temp_monitor_error_t map_esp_err_to_temp_monitor_error(esp_err_t err);

// ----------------------------
// Task
// ----------------------------
static void temp_monitor_task(void *args)
{
    LOGGER_LOG_INFO(TAG, "Temperature monitor task started");
    TickType_t last_wake = xTaskGetTickCount();
    CHECK_ERR_LOG(post_temp_monitor_event(TEMP_MONITOR_READY, NULL, 0), "Failed to post TEMP_MONITOR_READY event");

    static uint8_t samples_collected = 0;
    static const uint8_t samples_per_scond = CONFIG_TEMP_SENSORS_SAMPLING_FREQ_HZ;
    static const uint8_t delay_between_samples = 1000 / samples_per_scond;
    const TickType_t period = pdMS_TO_TICKS(delay_between_samples);

    while (monitor_running)
    {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        esp_err_t ret;

        // Read temperature sensors data
        for (uint8_t retry = 0; retry < CONFIG_TEMP_SENSOR_MAX_READ_RETRIES; retry++)
        {
            ret = read_temp_sensors_data(temp_sample.sensors);

            if (ret == ESP_OK)
            {
                break;
            }

            LOGGER_LOG_WARN(TAG, "Retrying to read temperature sensors data (%d/%d)", retry + 1, CONFIG_TEMP_SENSOR_MAX_READ_RETRIES);

            vTaskDelay(pdMS_TO_TICKS(CONFIG_TEMP_SENSOR_RETRY_DELAY_MS));
        }

        if (ret != ESP_OK)
        {
            post_temp_monitor_error(map_esp_err_to_temp_monitor_error(ret), ret);
            LOGGER_LOG_ERROR(TAG, "Failed to get temperatures after retries: %s",
                             esp_err_to_name(ret));
            continue;
        }

        temp_ring_buffer_push(&temp_sample);

        // Post buffer ready event after samples collected
        samples_collected++;
        if (samples_collected >= samples_per_scond)
        {
            samples_collected = 0;
            xEventGroupSetBits(temp_processor_event_group, TEMP_READY_EVENT_BIT);
        }

        vTaskDelayUntil(&last_wake, period);
    }

    LOGGER_LOG_INFO(TAG, "Temperature monitor task exiting");
    vTaskDelete(NULL);
}

esp_err_t start_temperature_monitor_task(void)
{
    if (monitor_running)
    {
        return ESP_OK;
    }

    temp_sample.number_of_attached_sensors = temp_monitor.number_of_attached_sensors;

    monitor_running = true;

    CHECK_ERR_LOG_CALL_RET(xTaskCreate(
                               temp_monitor_task,
                               temp_monitor_config.task_name,
                               temp_monitor_config.stack_size,
                               NULL,
                               temp_monitor_config.task_priority,
                               &temp_monitor_task_handle) == pdPASS
                               ? ESP_OK
                               : ESP_FAIL,
                           monitor_running = false,
                           "Failed to create temperature monitor task");

    return ESP_OK;
}

esp_err_t stop_temperature_monitor_task(void)
{
    if (!monitor_running)
    {
        return ESP_OK;
    }

    monitor_running = false;

    if (temp_monitor_task_handle)
    {
        vTaskDelete(temp_monitor_task_handle);
        temp_monitor_task_handle = NULL;
    }

    return ESP_OK;
}

// Classify sensor error based on internal sensor data
static temp_sensor_fault_type_t classify_sensor_fault(temp_sensor_t *sensor_data)
{
    if (sensor_data->sensor_fault.raw_fault_byte == 0)
    {
        return SENSOR_ERR_NONE;
    }
    else if (sensor_data->sensor_fault.faults.rtdin_force_open || sensor_data->sensor_fault.faults.refin_force_open || sensor_data->sensor_fault.faults.refin_force_closed)
    {
        return SENSOR_ERR_RTD_FAULT;
    }
    else if (sensor_data->sensor_fault.faults.over_under_voltage)
    {
        return SENSOR_ERR_COMMUNICATION;
    }
    else
    {
        return SENSOR_ERR_UNKNOWN;
    }
}

// Post temperature monitor error event
static esp_err_t post_temp_monitor_error(temp_monitor_error_t type, esp_err_t esp_err_code)
{
    temp_monitor_error_data_t error_data = {
        .type = type,
        .esp_err_code = esp_err_code};

    CHECK_ERR_LOG_RET(post_temp_monitor_event(TEMP_MONITOR_ERROR_OCCURRED, &error_data, sizeof(error_data)), "Failed to post temperature monitor error event");

    return ESP_OK;
}

static esp_err_t post_temp_monitor_event(temp_monitor_event_t event_type, void *event_data, size_t event_data_size)
{

    CHECK_ERR_LOG_RET(esp_event_post_to(
                          temp_monitor.temperature_event_loop_handle,
                          TEMP_MONITOR_EVENT,
                          event_type,
                          event_data,
                          event_data_size,
                          portMAX_DELAY),
                      "Failed to post temperature monitor event");

    return ESP_OK;
}

// Map esp_err_t to temp_monitor_error_data_t
static temp_monitor_error_t map_esp_err_to_temp_monitor_error(esp_err_t err)
{
    switch (err)
    {
    case ESP_ERR_INVALID_ARG:
    case ESP_ERR_INVALID_STATE:
        return TEMP_MONITOR_ERR_SENSOR_READ;
    case ESP_ERR_TIMEOUT:
        return TEMP_MONITOR_ERR_SPI;
    default:
        return TEMP_MONITOR_ERR_UNKNOWN;
    }
}
