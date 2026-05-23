#pragma once

#include <esp_err.h>
#include <stdint.h>

void opt4001_i2c_scan(void);
esp_err_t opt4001_init(void);
esp_err_t opt4001_read_lux(float *lux);
