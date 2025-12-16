#ifndef HEALTH_MONITOR_INTERNAL_H
#define HEALTH_MONITOR_INTERNAL_H

#include "esp_err.h"
#include <stdbool.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

typedef enum
{
    HB_STATE_OK,
    HB_STATE_LATE,
    HB_STATE_MISSED,
    HB_STATE_FAILED,
} heartbeat_state_t;

typedef struct
{
    TickType_t last_seen_tick;
    TickType_t max_silence_ticks;

    uint8_t miss_count;
    uint8_t max_misses;

    bool required;
    heartbeat_state_t state;
} heartbeat_entry_t;

typedef struct
{
    heartbeat_entry_t heartbeat[CONFIG_HEARTH_BEAT_COUNT];
    volatile bool is_running;
    bool events_initialized;
    bool tasks_initialized;
    bool initialized;
    TaskHandle_t task_handle;
} health_monitor_ctx_t;

esp_err_t init_health_monitor_task(health_monitor_ctx_t* ctx);
esp_err_t shutdown_health_monitor_task(health_monitor_ctx_t* ctx);

esp_err_t init_health_monitor_events(health_monitor_ctx_t* ctx);
esp_err_t shutdown_health_monitor_events(health_monitor_ctx_t* ctx);

#endif // HEALTH_MONITOR_INTERNAL_H
