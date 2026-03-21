#include "temperature_monitor_internal.h"
#include "temperature_monitor_component.h"
#include "sdkconfig.h"

bool temp_ring_buffer_init(temp_ring_buffer_t *rb)
{
    rb->write_index = 0;
    rb->read_index = 0;
    rb->count = 0;
    rb->mutex = xSemaphoreCreateMutex();
    return (rb->mutex != NULL);
}

void temp_ring_buffer_push(temp_ring_buffer_t *rb, const temp_sample_t *sample)
{
    xSemaphoreTake(rb->mutex, portMAX_DELAY);

    const size_t cap = CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE;

    rb->buffer[rb->write_index] = *sample;

    rb->write_index++;
    if (rb->write_index == cap)
        rb->write_index = 0;

    if (rb->count < cap) {
        rb->count++;
    } else {
        // Buffer full -> drop oldest
        rb->read_index++;
        if (rb->read_index == cap)
            rb->read_index = 0;
    }

    xSemaphoreGive(rb->mutex);
}

size_t temp_ring_buffer_pop_all_internal(temp_ring_buffer_t *rb, temp_sample_t *out_dest, size_t max_out)
{
    xSemaphoreTake(rb->mutex, portMAX_DELAY);

    size_t count = rb->count;
    if (count > max_out)
        count = max_out;

    size_t cap = CONFIG_TEMP_SENSORS_RING_BUFFER_SIZE;
    size_t idx = rb->read_index;

    for (size_t i = 0; i < count; i++) {
        out_dest[i] = rb->buffer[idx];
        idx++;
        if (idx == cap)
            idx = 0;
    }

    // consume
    rb->read_index = idx;
    rb->count -= count;

    xSemaphoreGive(rb->mutex);
    return count;
}

// Public API wrapper
size_t temp_ring_buffer_pop_all(temp_sample_t *out_dest, size_t max_out)
{
    if (g_temp_monitor_ctx == NULL)
    {
        return 0;
    }
    return temp_ring_buffer_pop_all_internal(&g_temp_monitor_ctx->ring_buffer, out_dest, max_out);
}
