#include "temperature_processor_internal.h"
#include "logger_component.h"
#include "sdkconfig.h"
#include <float.h>

static const char* TAG = "TEMP_PROCESSOR";

static process_temp_result_t process_temperature_data(temp_sample_t* input_temperatures,
                                                      float* output_temperature);

static temp_anomaly_result_t check_temperature_anomalies(temp_sample_t* temperatures);

static inline float calculate_average_temperature(temp_sample_t* temperatures);

static inline float average_float_array(const float* array, size_t count);

process_temp_samples_result_t process_temperature_samples(temp_processor_context_t* ctx, temp_sample_t* input_samples,
                                                          size_t number_of_samples, float* output_temperature)
{
    process_temp_samples_result_t result = {0};
    if (number_of_samples == 0 || input_samples == NULL || output_temperature == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid input to process_temperature_samples");
        result.error_type = PROCESS_TEMPERATURE_ERROR_INVALID_DATA;
        return result;
    }

    float min = FLT_MAX;
    float max = FLT_MIN;
    float* temperatures_buffer = ctx->temperatures_buffer;
    for (size_t i = 0; i < number_of_samples; i++)
    {
        process_temp_result_t process_temp_result =
            process_temperature_data(&input_samples[i], &temperatures_buffer[i]);
        if (temperatures_buffer[i] < min)
            min = temperatures_buffer[i];
        if (temperatures_buffer[i] > max)
            max = temperatures_buffer[i];
        if (process_temp_result.error_type != PROCESS_TEMPERATURE_ERROR_NONE)
        {
            result.error_type = process_temp_result.error_type;
            result.process_temp_result_errors[result.number_of_error_results++] = process_temp_result;
            LOGGER_LOG_WARN(TAG, "Error processing temperature sample %d: error type %d", i, result.error_type);
        }
    }

    LOGGER_LOG_INFO(TAG, "Temperature samples range: %.2f°C - %.2f°C", min, max);
    if (max - min > CONFIG_TEMP_DELTA_THRESHOLD)
    {
        result.error_type = PROCESS_TEMPERATURE_ERROR_THRESHOLD_EXCEEDED;
        LOGGER_LOG_WARN(TAG, "Temperature delta %.2f°C exceeds threshold %.2f°C", max-min, CONFIG_TEMP_DELTA_THRESHOLD);
    }


    // Calculate overall average temperature
    *output_temperature = average_float_array(temperatures_buffer, number_of_samples);

    return result;
}

static process_temp_result_t process_temperature_data(temp_sample_t* input_temperatures,
                                                      float* output_temperature)
{
    process_temp_result_t result = {0};

    if (input_temperatures->number_of_attached_sensors == 0)
    {
        LOGGER_LOG_ERROR(TAG, "No attached temperature sensors");
        result.error_type = PROCESS_TEMPERATURE_ERROR_INVALID_DATA;
        return result;
    }

    // Check for anomalies
    result.anomaly_result = check_temperature_anomalies(input_temperatures);
    if (result.anomaly_result.anomaly_count > 0)
    {
        result.error_type = PROCESS_TEMPERATURE_ERROR_THRESHOLD_EXCEEDED;
    }

    // Calculate average temperature
    *output_temperature = calculate_average_temperature(input_temperatures);

    return result;
}

static temp_anomaly_result_t check_temperature_anomalies(temp_sample_t* temperatures)
{
    temp_sensor_t* sensors = temperatures->sensors;
    size_t count = temperatures->number_of_attached_sensors;
    temp_anomaly_result_t result = {0};
    if (count < 2)
    {
        return result;
    }
    const float temp_delta_threshold = CONFIG_TEMP_DELTA_THRESHOLD;

    for (size_t i = 1; i < count; i++)
    {
        const float delta = sensors[i].temperature_c - sensors[i - 1].temperature_c;
        if (delta > temp_delta_threshold)
        {
            LOGGER_LOG_WARN(TAG, "Anomaly detected between sensors %d and %d: Δ%.2f°C exceeds threshold %.2f°C",
                            sensors[i - 1].index, sensors[i].index, delta, temp_delta_threshold);
            result.temp_sensor_pairs[result.anomaly_count++] = (temp_sensor_pair_t)
            {
                .first_sensor_index = sensors[i - 1].index,
                .second_sensor_index = sensors[i].index,
                .temp_delta = delta
            };
        }
    }

    return result;
}

static inline float average_float_array(const float* array, const size_t count)
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
        const float t = array[i];
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

static inline float calculate_average_temperature(temp_sample_t* temperatures)
{
    const size_t count = temperatures->number_of_attached_sensors;
    const temp_sensor_t* sensors = temperatures->sensors;

    if (count == 0)
    {
        return 0.0f;
    }

    float sum = 0.0f;
    float min = FLT_MAX;
    float max = FLT_MIN;

    for (size_t i = 0; i < count; i++)
    {
        const float t = sensors[i].temperature_c;
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
