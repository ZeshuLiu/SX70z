/* pcf8575.c - PCF8575 I2C 16-bit GPIO expander driver (ported to ESP-IDF) */

#include "pcf8575.h"
#include <esp_log.h>

static const char *TAG = "PCF8575";

int pcf8575_init(pcf8575_t *dev, i2c_port_t i2c_port, uint8_t i2c_addr, uint16_t pin_dir_mask) {
    if (!dev || i2c_addr < 0x20 || i2c_addr > 0x27) {
        return -1;
    }

    dev->i2c_port = i2c_port;
    dev->i2c_addr = i2c_addr;
    dev->pin_dir_mask = pin_dir_mask;
    dev->pin_state = pin_dir_mask & 0xFFFF;

    uint8_t buf[2];
    buf[0] = dev->pin_state & 0xFF;
    buf[1] = (dev->pin_state >> 8) & 0xFF;

    esp_err_t ret = i2c_master_write_to_device(dev->i2c_port, dev->i2c_addr,
                                                buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init write failed: %s", esp_err_to_name(ret));
        return -1;
    }
    return 0;
}

uint16_t pcf8575_read(pcf8575_t *dev) {
    uint8_t buf[2];

    esp_err_t ret = i2c_master_read_from_device(dev->i2c_port, dev->i2c_addr,
                                                buf, 2, pdMS_TO_TICKS(100));
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Read failed: %s", esp_err_to_name(ret));
        return 0xFFFF;
    }

    return ((uint16_t)buf[1] << 8) | buf[0];
}

void pcf8575_write(pcf8575_t *dev, uint16_t value) {
    uint8_t buf[2];

    uint16_t output_value = (value & ~dev->pin_dir_mask) | (dev->pin_dir_mask & 0xFFFF);

    buf[0] = output_value & 0xFF;
    buf[1] = (output_value >> 8) & 0xFF;

    dev->pin_state = output_value;

    i2c_master_write_to_device(dev->i2c_port, dev->i2c_addr,
                                buf, 2, pdMS_TO_TICKS(100));
}

void pcf8575_set_output(pcf8575_t *dev, uint8_t pin) {
    if (pin > 15) return;

    dev->pin_dir_mask &= ~(1U << pin);
    pcf8575_write(dev, dev->pin_state);
}

void pcf8575_set_input(pcf8575_t *dev, uint8_t pin) {
    if (pin > 15) return;

    dev->pin_dir_mask |= (1U << pin);

    uint16_t new_state = dev->pin_state | (1U << pin);
    pcf8575_write(dev, new_state);
}

void pcf8575_write_pin(pcf8575_t *dev, uint8_t pin, uint8_t value) {
    if (pin > 15) return;

    if (dev->pin_dir_mask & (1U << pin)) {
        return;
    }

    if (value) {
        dev->pin_state |= (1U << pin);
    } else {
        dev->pin_state &= ~(1U << pin);
    }

    pcf8575_write(dev, dev->pin_state);
}

uint8_t pcf8575_read_pin(pcf8575_t *dev, uint8_t pin) {
    if (pin > 15) return 0;

    uint16_t state = pcf8575_read(dev);
    return (state & (1U << pin)) ? 1 : 0;
}

uint16_t pcf8575_get_state(pcf8575_t *dev) {
    return dev->pin_state;
}
