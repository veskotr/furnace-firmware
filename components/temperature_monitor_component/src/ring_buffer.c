#include "ring_buffer.h"
#include "temperature_monitor_component.h"
#include "sdkconfig.h"

typedef struct
{
    temp_sample_t buffer[CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE];
    size_t write_index;
    size_t read_index;
    size_t count;
    SemaphoreHandle_t mutex;
} temp_ring_buffer_t;

static temp_ring_buffer_t temp_data_ring_buffer;

bool temp_ring_buffer_init()
{
    temp_data_ring_buffer.write_index = 0;
    temp_data_ring_buffer.read_index = 0;
    temp_data_ring_buffer.count = 0;
    temp_data_ring_buffer.mutex = xSemaphoreCreateMutex();
    return (temp_data_ring_buffer.mutex != NULL);
}

void temp_ring_buffer_push(const temp_sample_t *sample)
{
    xSemaphoreTake(temp_data_ring_buffer.mutex, portMAX_DELAY);

    size_t cap = CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE;

    temp_data_ring_buffer.buffer[temp_data_ring_buffer.write_index] = *sample;

    temp_data_ring_buffer.write_index++;
    if (temp_data_ring_buffer.write_index == cap)
        temp_data_ring_buffer.write_index = 0;

    if (temp_data_ring_buffer.count < cap) {
        temp_data_ring_buffer.count++;
    } else {
        // Buffer full -> drop oldest
        temp_data_ring_buffer.read_index++;
        if (temp_data_ring_buffer.read_index == cap)
            temp_data_ring_buffer.read_index = 0;
    }

    xSemaphoreGive(temp_data_ring_buffer.mutex);
}

size_t temp_ring_buffer_pop_all(temp_sample_t *out_dest, size_t max_out)
{
    xSemaphoreTake(temp_data_ring_buffer.mutex, portMAX_DELAY);

    size_t count = temp_data_ring_buffer.count;
    if (count > max_out)
        count = max_out;

    size_t cap = CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE;
    size_t idx = temp_data_ring_buffer.read_index;

    for (size_t i = 0; i < count; i++) {
        out_dest[i] = temp_data_ring_buffer.buffer[idx];
        idx++;
        if (idx == cap)
            idx = 0;
    }

    // consume
    temp_data_ring_buffer.read_index = idx;
    temp_data_ring_buffer.count -= count;

    xSemaphoreGive(temp_data_ring_buffer.mutex);
    return count;
}
