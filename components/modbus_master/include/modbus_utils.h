//
// Created by vesko on 14.3.2026 г..
//
#pragma once

#include "inttypes.h"


float modbus_master_swap_float_cdab(const uint16_t *registers, uint32_t *raw_output);
uint16_t modbus_master_swap_u16(uint16_t value);