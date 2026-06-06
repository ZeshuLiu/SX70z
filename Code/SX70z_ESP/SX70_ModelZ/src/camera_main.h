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

/** 电磁铁 PWM 占空比（LEDC 8-bit, 31.25kHz） */
#define SOL1_DUTY_FULL  255   // SOL1 快门 100% 吸合
#define SOL1_DUTY_HOLD  102   // SOL1 快门 40% 保持
#define SOL2_DUTY_ON    255   // SOL2 光圈吸合
#define SOL2_DUTY_OFF   0     // SOL2 光圈释放

/** 系统窗口衰减系数 — 校准 OPT4001 读数以匹配真实场景照度 */
#define METERING_ATTEN_K  256.0f

/** 快门速度档位总数 */
#define SHUTTER_SPEED_COUNT 27

/** 菜单模式 */
#define MENU_AUTO           0
#define MENU_BULB           1    // BULB/TIME 合并
#define MENU_MANUAL         2    // 原 menu 3 重编号
#define MENU_COUNT          3    // 拍摄菜单项数量
#define MENU_SETTINGS_START 10
#define MENU_SELF_TIMER     MENU_SETTINGS_START

/** 功能开关 */
#define HAS_FOCUS 0  // 对焦功能待实现

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

    uint8_t menu;               // 菜单层级：0=AUTO, 1=BULB/TIME, 2=MANUAL, 10=自拍
    uint16_t time_mode;         // menu 1 子模式：0=B, 1=T, 2+=秒值(2,4,8...翻倍)
    char cam_mode[10];           // 当前模式字符串
    char shut_mode;             // 快门模式: '1'=正常, 'B'=B 门, 'T'=T 门
    uint8_t shutter_speed;      // 当前快门速度索引
    uint8_t self_timer_sec;     // 自拍定时设置值：0/2/5/10
    int8_t multi_exp_remain;    // 额外多重曝光张数：0=正常拍一张 N=正常拍N张，-1=仅中止（首张防护，仍拍1张）

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

/** GPTimer 微秒级延时（Core 1 忙等，不释放 CPU） */
void delay_us(uint32_t us);
