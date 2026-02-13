#pragma once

#include <stdbool.h>
#include "sdkconfig.h"
#include "event_registry.h"

/**
 * Command types for the HMI coordinator queue.
 *
 * All work that touches shared HMI state (draft, display, SD card) flows
 * through this queue so that a single coordinator task serializes it.
 */
typedef enum {
    HMI_CMD_HANDLE_LINE,        // RX task received a complete Nextion line
    HMI_CMD_INIT_DISPLAY,       // Push initial UI state after boot

    // --- Event-driven updates (Phase 4) ---
    HMI_CMD_TEMP_UPDATE,        // Temperature processor event → live temp display
    HMI_CMD_PROFILE_STARTED,    // Coordinator: profile execution started
    HMI_CMD_PROFILE_PAUSED,     // Coordinator: profile execution paused
    HMI_CMD_PROFILE_RESUMED,    // Coordinator: profile execution resumed
    HMI_CMD_PROFILE_STOPPED,    // Coordinator: profile execution stopped
    HMI_CMD_PROFILE_ERROR,      // Coordinator: error occurred
} hmi_cmd_type_t;

/**
 * Command envelope posted to the coordinator queue.
 *
 * For HANDLE_LINE the `line` field carries the cleaned Nextion protocol line.
 * For INIT_DISPLAY no payload is needed.
 * For TEMP_UPDATE the `temp` field carries the live temperature data.
 * For PROFILE_* the `profile_event` field carries coordinator state.
 */
typedef struct {
    hmi_cmd_type_t type;
    union {
        char line[CONFIG_NEXTION_LINE_BUF_SIZE];     // HMI_CMD_HANDLE_LINE
        struct {
            float average_temperature;               // HMI_CMD_TEMP_UPDATE
            bool valid;
        } temp;
        struct {
            float current_temperature;               // HMI_CMD_PROFILE_*
            float target_temperature;
            uint32_t elapsed_ms;
            uint32_t total_ms;
        } profile_event;
        struct {
            coordinator_error_code_t error_code;     // HMI_CMD_PROFILE_ERROR
            esp_err_t esp_error;
        } error;
    };
} hmi_cmd_t;

/**
 * Create the command queue, subscribe to system events, and start the
 * coordinator task. Called once from nextion_hmi_init().
 */
void hmi_coordinator_init(void);

/**
 * Post a received line for the coordinator to process.
 * Called from the RX task after reassembling a complete Nextion line.
 * Returns true if the command was queued, false if the queue is full.
 */
bool hmi_coordinator_post_line(const char *line);

/**
 * Post a command with no payload (e.g. HMI_CMD_INIT_DISPLAY).
 * Returns true if the command was queued.
 */
bool hmi_coordinator_post_cmd(hmi_cmd_type_t type);

