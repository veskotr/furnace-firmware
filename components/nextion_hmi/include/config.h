#pragma once

#include <stdbool.h>

typedef struct {
    int max_operational_time_min;
    int min_operational_time_min;
    int max_temperature_c;
    int sensor_read_frequency_sec;
    int delta_t_max_per_min_x10;     // x10 fixed-point: 30 = 3.0°C/min max
    int delta_t_min_per_min_x10;     // x10 fixed-point: -30 = -3.0°C/min min (cooling)
    int time_tolerance_sec;          // Allowed time deviation for validation
    int temp_tolerance_c;            // Allowed temperature deviation for validation
    int delta_temp_tolerance_c_x10;  // Allowed delta temp deviation (x10 for 0.5 precision)
    int power_kw;                    // Device power (kW)
} AppConfig;

// Initialize config module (load from NVS)
void config_init(void);

// Get hardware/safety limits
AppConfig config_get_defaults(void);

// Get effective config (currently same as defaults)
AppConfig config_get_effective(void);
