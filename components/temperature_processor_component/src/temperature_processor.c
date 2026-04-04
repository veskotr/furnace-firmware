#include "temperature_processor_internal.h"
#include "logger_core.h"
#include "sdkconfig.h"
#include <float.h>

#include "utils.h"

static const char* TAG = "TEMP_PROCESSOR";


static esp_err_t check_temperature_anomalies(const float* temperatures, const size_t count);


static float average_float_array(const float* array, size_t count);

esp_err_t process_temperature_samples(temp_processor_context_t* ctx, const size_t number_of_samples,
                                      float* output_temperature)
{
    if (number_of_samples == 0 || output_temperature == NULL)
    {
        LOGGER_LOG_ERROR(TAG, "Invalid input to process_temperature_samples");
        return ESP_ERR_INVALID_ARG;
    }

    CHECK_ERR_LOG_RET(check_temperature_anomalies(ctx->temperatures_buffer, number_of_samples),
                      "Temperature anomaly detected in samples");

    float min = FLT_MAX;
    float max = FLT_MIN;
    for (size_t i = 0; i < number_of_samples; i++)
    {
        if (ctx->temperatures_buffer[i] < min)
            min = ctx->temperatures_buffer[i];
        if (ctx->temperatures_buffer[i] > max)
            max = ctx->temperatures_buffer[i];
    }

    LOGGER_LOG_INFO(TAG, "Temperature samples range: %.2f°C - %.2f°C", min, max);
    if (max - min > CONFIG_TEMP_DELTA_THRESHOLD)
    {
        LOGGER_LOG_WARN(TAG, "Temperature delta %.2f°C exceeds threshold %.2f°C", max-min, CONFIG_TEMP_DELTA_THRESHOLD);
        return ESP_ERR_INVALID_STATE;
    }

    // Calculate overall average temperature
    *output_temperature = average_float_array(ctx->temperatures_buffer, number_of_samples);

    return ESP_OK;
}

static esp_err_t check_temperature_anomalies(const float* temperatures, const size_t count)
{
    if (count < 2)
    {
        return ESP_ERR_INVALID_STATE;
    }

    for (size_t i = 1; i < count; i++)
    {
        const float temp_delta_threshold = CONFIG_TEMP_DELTA_THRESHOLD;
        const float delta = temperatures[i] - temperatures[i - 1];
        if (delta > temp_delta_threshold)
        {
            LOGGER_LOG_WARN(TAG, "Anomaly detected: Δ%.2f°C exceeds threshold %.2f°C", delta, temp_delta_threshold);
            return ESP_ERR_INVALID_STATE;
        }
    }

    return ESP_OK;
}

static float average_float_array(const float* array, const size_t count)
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

    return sum / (float)count;
}
