#pragma once

#include "camera_main.h"
#include "ssd1306.h"

/** 绘制一帧显示到 OLED */
void display_show_frame(const camera_state_t *state, ssd1306_t *disp);

/** 拍摄模式数据显示（快门速度/测光/闪光/模式，menu<10 时由 frame 调用） */
void display_show_taking(const camera_state_t *state, ssd1306_t *disp);

/** 设置菜单数据显示（自拍定时等，menu=10 时由 frame 调用） */
void display_show_menu(const camera_state_t *state, ssd1306_t *disp);

/** 倒计时专用显示（清屏后只显示剩余秒数） */
void display_show_countdown(ssd1306_t *disp, int8_t seconds_remaining);
