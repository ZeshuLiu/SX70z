#pragma once

#include "camera_main.h"
#include "ssd1306.h"

/** 绘制一帧显示到 OLED */
void display_show_frame(const camera_state_t *state, ssd1306_t *disp);
