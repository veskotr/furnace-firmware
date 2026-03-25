//
// Created by vesko on 22.3.2026 г..
//
#pragma once
#include "logger_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

esp_err_t logger_init_storage(void);

void store_log_entry(const log_level_t level, const char* tag, const char* message);

