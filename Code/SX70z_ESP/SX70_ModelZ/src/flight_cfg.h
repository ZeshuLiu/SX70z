#pragma once

#include <stdint.h>
#include "esp_err.h"

/** 从 NVS 加载飞行模式标志，未存过默认 0（关闭） */
esp_err_t flight_cfg_init(void);

/** 读取飞行模式：0=关闭, 1=开启 */
int8_t flight_cfg_get(void);

/** 保存飞行模式到 NVS */
esp_err_t flight_cfg_set(int8_t mode);
