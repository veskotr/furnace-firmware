#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include "temperature_monitor_types.h"

bool temp_ring_buffer_init();

void temp_ring_buffer_push(const temp_sample_t *sample);

#endif // RING_BUFFER_H