//
// Created by vesko on 14.3.2026 г..
//

#include <string.h>
#include "modbus_utils.h"
#include "modbus_master.h"

float modbus_master_swap_float_cdab(const uint16_t *registers, uint32_t *raw_output)
{
    const uint16_t reg_high = registers[1];
    const uint16_t reg_low = registers[0];

    *raw_output = ((uint32_t)reg_high << 16) | reg_low;

    float result;
    memcpy(&result, raw_output, sizeof(result));
    return result;
}

uint16_t modbus_master_swap_u16(const uint16_t value)
{
    return (value >> 8) | (value << 8);
}