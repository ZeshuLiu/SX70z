#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/** 任务优先级 */
#define SHUTTER_TASK_PRIO    10  // 强时序任务：最高优先级，阻塞所有 Core 1 任务
#define CONTROL_TASK_PRIO   8   // 控制任务：按键/闪光/S1（时序敏感）
#define DISPLAY_TASK_PRIO   5   // 显示任务：I2C 刷屏（与控制解耦，避免阻塞按键）
#define METERING_TASK_PRIO  3   // 测光任务：低优先级（1s 周期）

/** 系统窗口衰减系数 — 校准 OPT4001 读数以匹配真实场景照度
 *  raw_lux × METERING_ATTEN_K = 标定后 lux
 *  大于 1.0 = 补偿衰减，小于 1.0 = 抑制过曝
 *  调试方法：用独立测光表对比，调整此值直到 EV 读数一致
 */
#define METERING_ATTEN_K  256.0f

// 测光参数
typedef struct {
    float last_lux;             // 上次测光值 (lux, 校准后 = raw × METERING_ATTEN_K)
    float last_lux_raw;         // 原始 OPT4001 读数
    float ev;                   // 校准后 EV (ISO 640)
    float ev_raw;               // 原始 EV
    uint8_t auto_shutter_pos;   // AUTO 模式计算的快门速度索引
} metering_state_t;

// 按键参数
typedef struct {
    bool available;             // PCF8575 是否可用
    char old_value[4];          // 上次按键状态 "111"
    uint32_t debounce_last;     // 上次防抖时间 (ms)
    uint32_t push_down_start;   // 按下键开始时间 (ms)
} button_state_t;

// 相机参数
typedef struct {
    bool if_display;            // OLED 是否可用
    bool has_flash;             // 闪光灯是否接入（control_task 统一检测）

    metering_state_t metering;  // 测光
    button_state_t button;      // 按键

    uint8_t menu;               // 菜单层级：0=AUTO, 1=BULB, 2=TIME, 3=MANUAL, 10=自拍
    char cam_mode[8];           // 当前模式字符串
    char shut_mode;             // 快门模式：'0'=闪光，'1'=正常，'B'=B 门，'T'=T 门
    uint8_t shutter_speed;      // 当前快门速度索引

    bool test_led_level;        // LED 测试电平
} camera_state_t;

/** 控制任务句柄，供外部挂起/恢复 */
extern TaskHandle_t control_task_handle;

/** 全局相机状态，供 display / shutter 等模块读取 */
extern camera_state_t camera_state;

/** 相机控制任务 — 跑在 Core 1 */
void control_task(void *pvParameters);

/** OTA 期间挂起控制任务，避免 Flash 冲突 */
void camera_pause(void);

/** OTA 完成后恢复控制任务 */
void camera_resume(void);

/** 快门速度表查询（供 display / shutter 模块使用） */
const char *get_shutter_speed(uint8_t index);
uint16_t get_shutter_time_x10(uint8_t index);

/** 根据校准后 EV 计算快门速度索引 (F/8) */
uint8_t calc_shutter_from_ev(float ev);
