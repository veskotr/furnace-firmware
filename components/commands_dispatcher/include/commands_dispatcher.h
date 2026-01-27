#ifndef COMMANDS_DISPATCHER_H
#define COMMANDS_DISPATCHER_H
#include <stdbool.h>

#include "esp_err.h"

typedef enum
{
    COMMAND_TARGET_HEATER = 0,
    COMMAND_TARGET_COORDINATOR,
} command_target_t;

typedef enum
{
    COMMAND_TYPE_HEATER_SET_POWER = 0,
    COMMAND_TYPE_HEATER_GET_STATUS,
    COMMAND_TYPE_HEATER_TOGGLE,
} heater_command_type_t;

typedef struct
{
    heater_command_type_t type;
    float power_level; // 0.0 to 1.0,
    bool heater_state;
} heater_command_data_t;

typedef enum
{
    COMMAND_TYPE_COORDINATOR_START_PROFILE,
    COMMAND_TYPE_COORDINATOR_PAUSE_PROFILE,
    COMMAND_TYPE_COORDINATOR_RESUME_PROFILE,
    COMMAND_TYPE_COORDINATOR_STOP_PROFILE,
    COMMAND_TYPE_COORDINATOR_GET_STATUS_REPORT,
    COMMAND_TYPE_COORDINATOR_GET_CURRENT_PROFILE,
} coordinator_command_type_t;

typedef struct
{
    command_target_t target;
    void* data;
    size_t data_size;
} command_t;


typedef struct
{
    coordinator_command_type_t type;
    size_t profile_index;
} coordinator_command_data_t;

typedef esp_err_t (*command_handler_t)(void* handler_arg, void* command_data, size_t command_data_size);

esp_err_t commands_dispatcher_init(void);

esp_err_t commands_dispatcher_dispatch_command(command_t* command);

esp_err_t register_command_handler(
    command_target_t target,
    command_handler_t handler,
    void* handler_arg);

esp_err_t commands_dispatcher_shutdown(void);

#endif // COMMANDS_DISPATCHER_H
