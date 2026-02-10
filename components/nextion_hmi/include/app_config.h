#pragma once

#include "driver/uart.h"

// UART configuration for Nextion (dedicated UART)
#define NEXTION_UART_PORT UART_NUM_1
#define NEXTION_UART_BAUD_RATE 115200
#define NEXTION_UART_TX_PIN 32
#define NEXTION_UART_RX_PIN 33
#define NEXTION_UART_RX_BUF_SIZE 2048
#define NEXTION_UART_TX_BUF_SIZE 2048

// Nextion protocol
#define NEXTION_CMD_TERMINATOR 0xFF
#define NEXTION_CMD_TERMINATOR_COUNT 3

// Graph display dimensions (editable)
#define MAIN_GRAPH_WIDTH 1024
#define MAIN_GRAPH_HEIGHT 321
#define PROGRAMS_GRAPH_WIDTH 1024
#define PROGRAMS_GRAPH_HEIGHT 450

// Program editor constraints
#define PROGRAMS_PAGE_STAGE_COUNT 5
#define PROGRAMS_PAGE_COUNT 3
#define PROGRAMS_TOTAL_STAGE_COUNT (PROGRAMS_PAGE_STAGE_COUNT * PROGRAMS_PAGE_COUNT)

// Program storage limits
#define MAX_PROGRAMS 15

// Time constants
#define SECONDS_PER_MINUTE 60

// Nextion page names (must match HMI page names)
#define NEXTION_PAGE_MAIN "main_page"
#define NEXTION_PAGE_PROGRAMS "programs_page"
#define NEXTION_PAGE_SETTINGS "settings_page"

// Nextion component IDs (ask user for actual IDs before coding!)
#define NEXTION_GRAPH_DISP_ID 6
#define NEXTION_PROGRAMS_GRAPH_ID 51

// Config defaults (TODO: set to product-specific values)
#define CONFIG_MAX_OPERATIONAL_TIME_MIN 10080
#define CONFIG_MIN_OPERATIONAL_TIME_MIN 1
#define CONFIG_MAX_TEMPERATURE_C 200
#define CONFIG_SENSOR_READ_FREQUENCY_SEC 1
#define CONFIG_DELTA_T_MAX_PER_MIN_X10 30      // x10: 30 = 3.0°C/min max heating rate
#define CONFIG_DELTA_T_MIN_PER_MIN_X10 (-30)   // x10: -30 = -3.0°C/min max cooling rate
#define CONFIG_T_DELTA_MIN_MIN 1
#define CONFIG_HEATER_POWER 20

// Validation tolerances (for mathematical consistency checks)
// These allow small deviations when checking if user input is physically achievable
#define CONFIG_TIME_TOLERANCE_SEC 30      // Allow +/- 30 seconds deviation
#define CONFIG_TEMP_TOLERANCE_C 2         // Allow +/- 2°C deviation
#define CONFIG_DELTA_TEMP_TOLERANCE_C_X10 5 // x10: 5 = +/- 0.5°C/min deviation

// NVS key (reserved)
#define NVS_KEY_RTC_TIMESTAMP "rtc_ts"

// Program storage on Nextion SD
// Note: This is the max buffer size, not actual file size.
// Actual saved files only contain the serialized data (~800 bytes for 15 stages).
// Nextion SD bus limit is 4KB, so we use 4096 as the buffer limit.
#define PROGRAM_FILE_SIZE 4096
#define PROGRAM_FILE_EXTENSION ".prg"
