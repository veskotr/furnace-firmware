#include "temperature_processor_internal.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include <float.h>

static const char *TAG = "TEMP_PROCESSOR";

static process_temperature_error_t process_temperature_data(temp_sample_t *input_temperatures, float *output_temperature);

static process_temperature_error_t check_temperature_anomalies(temp_sample_t *temperatures);

static inline float calculate_average_temperature(temp_sample_t *temperatures);

static inline float average_float_array(const float *array, size_t count);

process_temperature_error_t process_temperature_samples(temp_processor_context_t *ctx, temp_sample_t *input_samples, size_t number_of_samples, float *output_temperature)
{
    process_temperature_error_t result = {0};
    if (number_of_samples == 0 || input_samples == NULL || output_temperature == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid input to process_temperature_samples");
        return (process_temperature_error_t){.error_type = PROCESS_TEMPERATURE_ERROR_INVALID_DATA, .sensor_index = 0};
    }

    float min = FLT_MAX;
    float max = FLT_MIN;
    float *temperatures_buffer = ctx->temperatures_buffer;
    for (size_t i = 0; i < number_of_samples; i++)
    {
        result = process_temperature_data(&input_samples[i], &temperatures_buffer[i]);
        if (temperatures_buffer[i] < min)
            min = temperatures_buffer[i];
        if (temperatures_buffer[i] > max)
            max = temperatures_buffer[i];
        if (result.error_type != PROCESS_TEMPERATURE_ERROR_NONE)
        {
            LOGGER_LOG_WARN(TAG, "Error processing temperature sample %d: error type %d", i, result.error_type);
        }
    }

    LOGGER_LOG_INFO(TAG, "Temperature samples range: %.2f°C - %.2f°C", min, max);
    if (max-min > CONFIG_TEMP_DELTA_THRESHOLD)
    {
        result.error_type = PROCESS_TEMPERATURE_THRESHOLD_EXCEEDED;
        LOGGER_LOG_WARN(TAG, "Temperature delta %.2f°C exceeds threshold %.2f°C", max-min, CONFIG_TEMP_DELTA_THRESHOLD);
        return result;
    }
    

    // Calculate overall average temperature
    *output_temperature = average_float_array(temperatures_buffer, number_of_samples);

    return result;
}

static process_temperature_error_t process_temperature_data(temp_sample_t *input_temperatures, float *output_temperature)
{
    process_temperature_error_t result;

    if (input_temperatures->number_of_attached_sensors == 0)
    {
        LOGGER_LOG_ERROR(TAG, "No attached temperature sensors");
        return (process_temperature_error_t){.error_type = PROCESS_TEMPERATURE_ERROR_INVALID_DATA, .sensor_index = 0};
    }

    // Check for anomalies
    result = check_temperature_anomalies(input_temperatures);

    // Calculate average temperature
    *output_temperature = calculate_average_temperature(input_temperatures);

    return result;
}

static process_temperature_error_t check_temperature_anomalies(temp_sample_t *temperatures)
{
    const float temp_delta_threshold = CONFIG_TEMP_DELTA_THRESHOLD;
    temp_sensor_t *sensors = temperatures->sensors;
    size_t count = temperatures->number_of_attached_sensors;

    if (count < 2)
    {
        return (process_temperature_error_t){.error_type = PROCESS_TEMPERATURE_ERROR_NONE, .sensor_index = 0};
    }

    for (size_t i = 1; i < count; i++)
    {
        float delta = sensors[i].temperature_c - sensors[i - 1].temperature_c;
        if (delta > temp_delta_threshold)
        {
            LOGGER_LOG_WARN(TAG, "Anomaly detected between sensors %d and %d: Δ%.2f°C exceeds threshold %.2f°C",
                            sensors[i - 1].index, sensors[i].index, delta, temp_delta_threshold);
            return (process_temperature_error_t){.error_type = PROCESS_TEMPERATURE_THRESHOLD_EXCEEDED, .sensor_index = sensors[i].index};
        }
    }

    return (process_temperature_error_t){.error_type = PROCESS_TEMPERATURE_ERROR_NONE, .sensor_index = 0};
}

static inline float average_float_array(const float *array, size_t count)
{
    if (count == 0 || array == NULL)
    {
        return 0.0f;
    }

    float sum = 0.0f;
    float min = FLT_MAX;
    float max = FLT_MIN;
    for (size_t i = 0; i < count; i++)
    {
        float t = array[i];
        sum += t;
        if (t < min)
            min = t;
        if (t > max)
            max = t;
    }
#if CONFIG_TEMP_SENSORS_HAVE_OUTLIERS_REJECTION
    if (count >= 3)
    {
        return (sum - min - max) / (float)(count - 2);
    }
    else
    {
        return sum / (float)count; /* fallback when not enough samples */
    }
#else
    return sum / (float)count;
#endif
}

static inline float calculate_average_temperature(temp_sample_t *temperatures)
{
    const size_t count = temperatures->number_of_attached_sensors;
    const temp_sensor_t *sensors = temperatures->sensors;

    if (count == 0)
    {
        return 0.0f;
    }

    float sum = 0.0f;
    float min = FLT_MAX;
    float max = FLT_MIN;

    for (size_t i = 0; i < count; i++)
    {
        float t = sensors[i].temperature_c;
        sum += t;

        if (t < min)
            min = t;
        if (t > max)
            max = t;
    }

#if CONFIG_TEMP_SENSORS_HAVE_OUTLIERS_REJECTION
    if (count >= 3)
    {
        return (sum - min - max) / (float)(count - 2);
    }
    else
    {
        return sum / (float)count; /* fallback when not enough samples */
    }
#else
    return sum / (float)count;
#endif
}