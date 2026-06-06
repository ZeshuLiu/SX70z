/* display_manager.c - OLED 显示帧绘制：模式/快门/测光信息布局 + 倒计时显示 */

#include "display_manager.h"
#include "fonts/font5x8.h"
#include <stdio.h>
#include <math.h>

// 拍摄模式字段索引 → 坐标 + 字体映射（布局固定，内容由调用者提供）
// 0: cam_mode  1: ISO  2: 闪光灯  3: 快门速度  4: 测光值
typedef struct {
    int16_t x, y;
    const ssd1306_font_t *font;
} field_t;

static const field_t fields[] = {
    {10,  2, &font5x8_font},  // 0: cam_mode
    {55,  2, &font5x8_font},  // 1: ISO
    {86,  2, &font5x8_font},  // 2: 闪光灯
    { 8, 18, &font5x8_font},  // 3: 快门速度
    {50, 24, &font5x8_font},  // 4: 测光值
};

static void draw_str_taking(ssd1306_t *disp, uint8_t index, const char *str)
{
    ssd1306_draw_str(disp, fields[index].x, fields[index].y, str, fields[index].font);
}

void display_show_frame(const camera_state_t *state, ssd1306_t *disp)
{
    if (!state->if_display) return;

    ssd1306_clear(disp);

    // 顶部状态栏 — 反色（白底黑字）
    ssd1306_fill_rect(disp, 0, 0, 128, 11);
    ssd1306_clear_rect(disp, 1, 1, 48, 9);    // 模式背景
    ssd1306_clear_rect(disp, 52, 1, 28, 9);   // ISO 背景
    ssd1306_clear_rect(disp, 51, 0, 1, 11);   // ISO 分隔线
    ssd1306_clear_rect(disp, 82, 0, 1, 11);   // 闪光灯分隔线

    if (state->menu == 10) {
        display_show_menu(state, disp);
    } else if (state->menu < 10) {
        display_show_taking(state, disp);
    }

    ssd1306_show(disp);
}

void display_show_taking(const camera_state_t *state, ssd1306_t *disp)
{
    draw_str_taking(disp, 0, state->cam_mode);
    draw_str_taking(disp, 1, "600");
    draw_str_taking(disp, 2, state->has_flash ? "FLASH" : "OFF");

    // 快门速度大字（AUTO 模式下实时显示测光结果）/ 自拍定时标记
    const char *speed_str = get_shutter_speed(
        (state->menu == 0) ? state->metering.auto_shutter_pos : state->shutter_speed);
    if (state->self_timer_sec > 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %ds", speed_str, state->self_timer_sec);
        draw_str_taking(disp, 3, buf);
    } else {
        draw_str_taking(disp, 3, speed_str);
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
        draw_str_taking(disp, 4, info_str);
    }
}

void display_show_menu(const camera_state_t *state, ssd1306_t *disp)
{
    draw_str_taking(disp, 0, state->cam_mode);
    draw_str_taking(disp, 1, "600");
    draw_str_taking(disp, 2, state->has_flash ? "FLASH" : "OFF");
    if (state->self_timer_sec > 0) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%ds", state->self_timer_sec);
        draw_str_taking(disp, 3, buf);
    } else {
        draw_str_taking(disp, 3, "OFF");
    }
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
