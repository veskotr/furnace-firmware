#ifndef CORE_TYPES_H
#define CORE_TYPES_H
#include <inttypes.h>
#include "esp_event.h"

typedef enum
{
    NODE_TYPE_LOG,
    NODE_TYPE_LINEAR,
    NODE_TYPE_SQUARE,
    NODE_TYPE_CUBE
} node_type_t;

typedef struct heating_node
{
    node_type_t type;
    struct heating_node *next_node;
    struct heating_node *previous_node;
    float set_temp;
    uint32_t duration_ms;
    char *expression;
} heating_node_t;

typedef struct
{
    char *name;
    heating_node_t *first_node;
} heating_profile_t;

#endif
