/* display_manager.c - OLED 显示帧绘制：模式/快门/测光信息布局 + 倒计时显示 */

#include "display_manager.h"
#include "fonts/font5x8.h"
#include "fonts/font6x12.h"
#include "fonts/font8x16.h"
#include <stdio.h>
#include <math.h>

// 拍摄模式字段索引 → 坐标 + 字体映射（布局固定，内容由调用者提供）
// 0: cam_mode  1: ISO  2: 闪光灯  3: 快门速度  4: 测光值
typedef struct {
    int16_t x, y;
    const ssd1306_font_t *font;
} field_t;

static const field_t fields[] = {
    {10,  2, &font6x12_font},  // 0: cam_mode
    {55,  2, &font6x12_font},  // 1: ISO
    {95,  1, &font6x12_font},  // 2: 自拍定时
    { 8, 22, &font5x8_font},  // 3: 快门速度
    {50, 22, &font5x8_font},  // 4: 测光值
};

static void draw_str_taking(ssd1306_t *disp, uint8_t index, const char *str)
{
    ssd1306_draw_str(disp, fields[index].x, fields[index].y, str, fields[index].font);
}

// 黑字版 draw_str（白底上画黑字）
static void draw_str_black(ssd1306_t *dev, int x, int y, const char *str, const ssd1306_font_t *font)
{
    while (*str) {
        uint8_t bpc = (font->height + 7) / 8;
        uint8_t stride = font->width * bpc;
        for (uint8_t i = 0; i < font->width; i++) {
            for (uint8_t b = 0; b < bpc; b++) {
                uint8_t line = font->data[(*str - font->first) * stride + i * bpc + b];
                for (uint8_t j = 0; j < 8 && (b * 8 + j) < font->height; j++) {
                    if (line & 0x01u)
                        ssd1306_clear_pixel(dev, x + i, y + b * 8 + j);
                    line >>= 1;
                }
            }
        }
        x += font->width;
        str++;
    }
}

void display_show_frame(const camera_state_t *state, ssd1306_t *disp)
{
    if (!state->if_display) return;

    ssd1306_clear(disp);

    // 顶部状态栏 — 反色，14px 适配 6×12 字体
    ssd1306_fill_rect(disp, 0, 0, 128, 15);
    ssd1306_clear_rect(disp, 1, 1, 48, 13);    // 模式背景
    ssd1306_clear_rect(disp, 52, 1, 25, 13);   // ISO 背景
    ssd1306_clear_rect(disp, 51, 0, 1, 15);   // ISO 分隔线
    ssd1306_clear_rect(disp, 79, 0, 1, 15);   // 闪光灯分隔线

    if (state->menu == MENU_SETTINGS_START) {
        display_show_menu(state, disp);
    } else if (state->menu < MENU_SETTINGS_START) {
        display_show_taking(state, disp);
    }

    ssd1306_show(disp);
}

// 通用位图绘制（row-based, MSB left，1=画黑点）
static void draw_bitmap(ssd1306_t *disp, int16_t x, int16_t y, uint8_t w, uint8_t h, const uint8_t *data)
{
    uint8_t bpr = (w + 7) / 8;
    for (int row = 0; row < h; row++) {
        for (int col = 0; col < w; col++) {
            if (data[row * bpr + col / 8] & (0x80 >> (col % 8)))
                ssd1306_clear_pixel(disp, x + col, y + row);
        }
    }
}

// 图标数据
extern const unsigned char flash_14_14[];

// 14×14 实心圆（无闪光灯）
// static const uint8_t circle_14[] = {
//     0x00,0x00, 0x0F,0x80, 0x1F,0xC0, 0x3F,0xE0,
//     0x3F,0xE0, 0x7F,0xF0, 0x7F,0xF0, 0x7F,0xF0,
//     0x7F,0xF0, 0x3F,0xE0, 0x3F,0xE0, 0x1F,0xC0,
//     0x0F,0x80, 0x00,0x00,
// };

void display_show_taking(const camera_state_t *state, ssd1306_t *disp)
{
    draw_str_taking(disp, 0, state->cam_mode);
    draw_str_taking(disp, 1, "600");
    if (state->has_flash) {
        draw_bitmap(disp, 80, 1, 14, 14, flash_14_14);
    } else {
        // draw_bitmap(disp, 83, 1, 14, 14, circle_14);
    }

    // 快门速度大字 / B-T/定时 模式
    const char *speed_str;
    if (state->menu == MENU_BULB) {
        // menu 1: 根据 time_mode 显示
        speed_str = (state->time_mode <= 1)
            ? (state->time_mode == 0 ? "B" : "T")
            : "00";  //TODO 显示定时时间 h min sec
    } else if (state->menu == MENU_AUTO) {
        speed_str = get_shutter_speed(state->metering.auto_shutter_pos);
    } else {
        speed_str = get_shutter_speed(state->shutter_speed);
    }
    draw_str_taking(disp, 3, speed_str);

    // 自拍定时（状态栏右上角）
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", state->self_timer_sec);
        draw_str_black(disp, 95, 1, state->self_timer_sec > 0 ? buf : "--", &font6x12_font);
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
    if (state->has_flash) {
        draw_bitmap(disp, 80, 1, 14, 14, flash_14_14);
    }
    {
        char buf[4];
        snprintf(buf, sizeof(buf), "%d", state->self_timer_sec);
        draw_str_black(disp, 95, 1, state->self_timer_sec > 0 ? buf : "--", &font6x12_font);
    }
}

void display_show_countdown(ssd1306_t *disp, int8_t seconds_remaining)
{
    if (seconds_remaining < 0) return;
    ssd1306_clear(disp);
    char buf[8];
    snprintf(buf, sizeof(buf), "%ds", seconds_remaining);
    // 居中显示（8×16 字体，"10s" = 3×8 = 24px 宽）
    ssd1306_draw_str(disp, 52, 8, buf, &font8x16_font);
    ssd1306_show(disp);
}
