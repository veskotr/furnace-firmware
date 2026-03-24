//
// Created by vesko on 22.3.2026 г..
//
#pragma once
#include "logger_component.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

extern SemaphoreHandle_t log_mutex;

void store_log_entry(const log_level_t level, const char* tag, const char* message);

