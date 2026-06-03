#include "display_manager.h"
#include "fonts/font5x8.h"
#include <stdio.h>
#include <math.h>

void display_show_frame(const camera_state_t *state, ssd1306_t *disp)
{
    if (!state->if_display) return;

    ssd1306_clear(disp);

    // 顶部状态栏 — 反色（白底黑字）
    ssd1306_fill_rect(disp, 0, 0, 128, 11);

    // 模式显示区
    ssd1306_clear_rect(disp, 1, 1, 48, 9);
    ssd1306_draw_str(disp, 10, 2, state->cam_mode, &font5x8_font);

    // ISO 显示
    ssd1306_clear_rect(disp, 52, 1, 28, 9);
    ssd1306_clear_rect(disp, 51, 0, 1, 11);   // 分隔线
    ssd1306_draw_str(disp, 55, 2, "600", &font5x8_font);

    // 闪光灯指示
    ssd1306_clear_rect(disp, 82, 0, 1, 11);   // 分隔线
    if (state->has_flash) {
        ssd1306_draw_str(disp, 86, 2, "FLASH", &font5x8_font);
    } else {
        ssd1306_draw_str(disp, 90, 2, "OFF", &font5x8_font);
    }

    // 快门速度大字（AUTO 模式下实时显示测光结果）/ 自拍定时
    if (state->menu == 10) {
        if (state->self_timer_sec > 0) {
            char buf[8];
            snprintf(buf, sizeof(buf), "%ds", state->self_timer_sec);
            ssd1306_draw_str(disp, 8, 18, buf, &font5x8_font);
        } else {
            ssd1306_draw_str(disp, 8, 18, "OFF", &font5x8_font);
        }
    } else {
        uint8_t disp_index = (state->menu == 0)
            ? state->metering.auto_shutter_pos
            : state->shutter_speed;
        const char *speed_str = get_shutter_speed(disp_index);
        if (state->self_timer_sec > 0) {
            char buf[16];
            snprintf(buf, sizeof(buf), "%s %ds", speed_str, state->self_timer_sec);
            ssd1306_draw_str(disp, 8, 18, buf, &font5x8_font);
        } else {
            ssd1306_draw_str(disp, 8, 18, speed_str, &font5x8_font);
        }
    }

    // 测光值（右下角，LUX 自适应小数位: 总数 6 位）
    {
        char info_str[32];
        float lux = state->metering.last_lux;
        int int_digits = (lux >= 1.0f) ? (int)log10f(lux) + 1 : 1;
        if (lux < 0.0f) int_digits = 1;
        int decimals = 6 - int_digits - 1;  // -1 for '.'
        if (decimals < 0) decimals = 0;
        if (decimals > 4) decimals = 4;
        snprintf(info_str, sizeof(info_str), "EV%.1f L%.*f",
                (double)state->metering.ev, decimals, (double)lux);
        ssd1306_draw_str(disp, 50, 24, info_str, &font5x8_font);
    }

    ssd1306_show(disp);
}

void display_show_countdown(ssd1306_t *disp, int8_t seconds_remaining)
{
    if (seconds_remaining < 0) return;
    ssd1306_clear(disp);
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", seconds_remaining);
    // 居中显示（5×8 字体，"10s" 最宽约 30px）
    ssd1306_draw_str(disp, 56, 12, buf, &font5x8_font);
    ssd1306_show(disp);
}
