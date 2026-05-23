/* pcf8575.h - PCF8575 I2C 16-bit GPIO expander driver */

#pragma once

#include <stdint.h>
#include <driver/i2c.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PCF8575_I2C_ADDR_BASE   0x20

typedef struct {
    i2c_port_t i2c_port;      // I2C_NUM_0 or I2C_NUM_1
    uint8_t i2c_addr;         // 0x20-0x27
    uint16_t pin_dir_mask;    // 1=input, 0=output
    uint16_t pin_state;       // current output state
} pcf8575_t;

int pcf8575_init(pcf8575_t *dev, i2c_port_t i2c_port, uint8_t i2c_addr, uint16_t pin_dir_mask);
uint16_t pcf8575_read(pcf8575_t *dev);
void pcf8575_write(pcf8575_t *dev, uint16_t value);
void pcf8575_set_output(pcf8575_t *dev, uint8_t pin);
void pcf8575_set_input(pcf8575_t *dev, uint8_t pin);
void pcf8575_write_pin(pcf8575_t *dev, uint8_t pin, uint8_t value);
uint8_t pcf8575_read_pin(pcf8575_t *dev, uint8_t pin);
uint16_t pcf8575_get_state(pcf8575_t *dev);

#ifdef __cplusplus
}
#endif
