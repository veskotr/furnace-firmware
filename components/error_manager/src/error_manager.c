#include "error_manager.h"
#include <sdkconfig.h>
#include "logger_component.h"

static const char* TAG = "ERROR_MANAGER";

static error_descriptor_func_t error_manager_funcs[CONFIG_ERROR_MANGER_MAX_MODULES] = {NULL};

void register_error_descriptor(const uint16_t component_id, const error_descriptor_func_t descriptor_func)
{
    if (component_id >= CONFIG_ERROR_MANGER_MAX_MODULES)
    {
        LOGGER_LOG_ERROR(TAG, "Component ID %d exceeds maximum allowed modules", component_id);
        return;
    }
    error_manager_funcs[component_id] = descriptor_func;
}

const char* get_error_description(const furnace_error_t* error)
{
    const uint16_t error_source = error->source;

    const uint16_t error_code = error->error_code;

    if (error_source >= CONFIG_ERROR_MANGER_MAX_MODULES || error_manager_funcs[error_source] == NULL)
    {
        return "Unknown component or no descriptor registered";
    }

    return error_manager_funcs[error_source](error_code);
}
