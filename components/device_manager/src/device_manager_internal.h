//
// Created by vesko on 9.3.2026 г..
//
#pragma once

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "device_manager.h"
#include "sdkconfig.h"
#include "esp_err.h"
#include "stdbool.h"

typedef enum
{
    DEVICE_FSM_IDLE = 0,
    DEVICE_FSM_WRITE_PENDING,
    DEVICE_FSM_WAIT_AFTER_WRITE,
    DEVICE_FSM_READ_PENDING
} device_fsm_state_t;

struct device
{
    uint16_t id;

    device_state_t state;
    device_type_t type;

    void* ctx;
    const char* name;

    const device_ops_t* ops;
};

typedef struct
{
    device_t devices[CONFIG_DEVICE_MANAGER_MAX_DEVICES];
    TaskHandle_t task_handle;
    uint8_t count;
    bool running;
} device_manager_context_t;

extern device_manager_context_t* g_device_manager_context;

esp_err_t init_device_manager_task(device_manager_context_t* ctx);

esp_err_t stop_device_manager_task(device_manager_context_t* ctx);
