#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/** 快门速度档位总数 */
#define SHUTTER_SPEED_COUNT 27

/** 启动时从 NVS 加载校准值，未存过则使用编译默认值 */
esp_err_t shutter_cal_init(void);

/** 将当前校准值写入 NVS */
esp_err_t shutter_cal_save(void);

/** 恢复出厂默认值（不会写入 NVS，需自行调用 shutter_cal_save） */
void shutter_cal_reset_defaults(void);

/** 当前校准值是否来自用户校准（非出厂默认） */
bool shutter_cal_is_valid(void);

/** 设置校准有效标志（POST /calibration 保存后调用） */
void shutter_cal_set_valid(bool valid);

/** 查询快门校准值 (0.1ms 单位)，index 自动夹紧 */
uint16_t get_shutter_time_x10(uint8_t index);

/** 查询快门速度标签字符串 */
const char *get_shutter_speed(uint8_t index);

/** 获取可写的校准数组指针（供 Web 端批量更新）
 *  注意：此函数会获取互斥锁，使用完毕后必须调用 shutter_cal_unlock() */
uint16_t *shutter_cal_get_array(void);

/** 释放由 shutter_cal_get_array() 获取的互斥锁 */
void shutter_cal_unlock(void);

/** 删除 NVS 中的校准数据，使重启后仍识别为"未校准" */
esp_err_t shutter_cal_erase(void);
