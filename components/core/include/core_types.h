#ifndef CORE_TYPES_H
#define CORE_TYPES_H
#include <inttypes.h>

typedef enum
{
    NODE_TYPE_LOG,
    NODE_TYPE_LINEAR,
    NODE_TYPE_SQUARE,
    NODE_TYPE_CUBE,
    NODE_TYPE_CUSTOM
} node_type_t;

typedef struct heating_node
{
    node_type_t type;
    struct heating_node *next_node;
    float set_temp;
    uint32_t duration_ms;
    char *expression;
} heating_node_t;

typedef struct
{
    char *name;
    heating_node_t *first_node;

} heating_program_t;

#endif
