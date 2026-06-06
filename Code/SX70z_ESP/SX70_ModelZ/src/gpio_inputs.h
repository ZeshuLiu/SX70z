#pragma once

#include <stdbool.h>
#include <stdint.h>

/** GPIO 去抖采样次数（连续 N 次一致才算确认） */
#define GPIO_DEBOUNCE_COUNT 5

/** S1 按键去抖采样次数 */
#define S1_DEBOUNCE_COUNT   5

/** 自拍定时选项个数 */
#define SELF_TIMER_OPTS_COUNT 4

/** GPIO 通用去抖（默认低电平，连续 N 次采样为高才算确认） */
bool gpio_debounce_defaultLow(int pin);

/** GPIO 通用去抖（默认高电平，连续 N 次采样为低才算确认） */
bool gpio_debounce_defaultHigh(int pin);

/** S1T 全按快门去抖，返回按下状态 */
bool debounce_read_s1pin(void);

/** S2 闪光灯检测（带防抖），返回是否接入 */
bool gpio_inputs_read_s2(void);

/** 3D 按键处理（PCF8575，100ms 防抖 + 边沿检测 + 长短按区分） */
void button3d_handler(void);

/** 根据 menu 更新 cam_mode 和 shut_mode */
void update_mode_display(void);

/** 菜单参数选择与调整状态（供 display 模块读取） */
extern int8_t menu_item_idx;
extern bool menu_adjust;
