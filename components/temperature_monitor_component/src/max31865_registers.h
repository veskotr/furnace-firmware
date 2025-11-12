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
    const uint8_t fault_status_read_address;
} max31865_registers_t;

typedef enum {
    MAX31865_FAULT_NONE           = 0x00,
    MAX31865_FAULT_HIGHTHRESH     = 0x80,
    MAX31865_FAULT_LOWTHRESH      = 0x40,
    MAX31865_FAULT_REFIN_FORCE_C  = 0x20,
    MAX31865_FAULT_REFIN_FORCE_O  = 0x10,
    MAX31865_FAULT_RTDIN_FORCE_O  = 0x08,
    MAX31865_FAULT_OV_UV          = 0x04,
} max31865_fault_t;

extern const max31865_registers_t max31865_registers;


#endif