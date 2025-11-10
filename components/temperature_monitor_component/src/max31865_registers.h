#ifndef MAX31865_REGISTERS_H
#define MAX31865_REGISTERS_H
#include <inttypes.h>

typedef struct
{
    const uint8_t config_register_read_address;
    const uint8_t config_register_write_address;
    const uint8_t rtd_msb_read_address ;
    const uint8_t rtd_lsb_read_address;
    const uint8_t high_fault_threshold_msb_read_address;
    const uint8_t high_fault_threshold_msb_write_address;
    const uint8_t high_fault_threshold_lsb_read_address;
    const uint8_t high_fault_threshold_lsb_write_address;
    const uint8_t low_fault_threshold_msb_read_address;
    const uint8_t low_fault_threshold_msb_write_address;
    const uint8_t low_fault_threshold_lsb_read_address;
    const uint8_t low_fault_threshold_lsb_write_address;
    const uint8_t fault_status;
} max31865_registers_t;

extern const max31865_registers_t max31865_registers;
#endif