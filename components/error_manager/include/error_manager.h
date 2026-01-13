#ifndef ERROR_MANAGER_H
#define ERROR_MANAGER_H

#include <stdint.h>
#include "furnace_error_types.h"

#define ERROR_CODE(type, sub_type, value, data) \
( ((uint32_t)(type)   << 24) | \
((uint32_t)(sub_type) << 16) | \
((uint32_t)(value)  << 8)  | \
((uint32_t)(data)) )

typedef const char* (*error_descriptor_func_t)(uint16_t error_code);

void register_error_descriptor(uint16_t component_id, error_descriptor_func_t descriptor_func);

const char* get_error_description(const furnace_error_t* error);

#endif
