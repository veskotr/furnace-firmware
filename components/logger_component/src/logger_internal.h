//
// Created by vesko on 22.3.2026 г..
//
#pragma once
#include "logger_component.h"
#include "freertos/FreeRTOS.h"

esp_err_t logger_init_storage(void);

esp_err_t logger_init_cli(void);

void store_log_entry(const log_level_t level, const char* tag, const char* message);

esp_err_t read_log(char *filename);

esp_err_t list_crash_logs(void);