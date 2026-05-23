#pragma once

#include <stdint.h>

/** 设备身份信息 */
typedef struct {
    char serial[13];    /* 序列号（芯片 MAC 地址） */
    uint8_t sw_major;   /* 软件主版本 */
    uint8_t sw_minor;   /* 软件次版本 */
    uint8_t sw_patch;   /* 软件修订号 */
    uint8_t hw_rev;     /* 硬件版本 */
    char build_time[24];    /* 编译时间（__DATE__ __TIME__） */
    char model[16];         /* 型号 */
} devinfo_t;

extern devinfo_t device;

/** 从芯片 eFuse 读取 MAC 填充序列号，需在 NVS 初始化后调用 */
void devinfo_init(void);
