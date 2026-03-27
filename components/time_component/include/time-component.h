#pragma once

#include "esp_err.h"

esp_err_t time_component_init(void);

void store_total_runtime_to_nvs(void);

uint32_t get_current_time_ms(void);

uint32_t get_time_since_boot_ms(void);

uint32_t get_total_runtime_sec(void);

esp_err_t time_component_shutdown(void);