/* Portrait OLED layout for the 32 x 128 logical canvas. */
#include "display_manager.h"
#include "gpio_inputs.h"
#include "shutter_cal.h"
#include "fonts/font5x8.h"
#include "fonts/font6x12.h"
#include "fonts/font_terminus8x14.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

#define SCREEN_WIDTH 32

static void centered(ssd1306_t *disp, int y, const char *text,
                     const ssd1306_font_t *font)
{
    int width = (int)strlen(text) * font->width;
    if (width <= SCREEN_WIDTH && width > 0)
        ssd1306_draw_str(disp, (SCREEN_WIDTH - width) / 2, y, text, font);
}

static void header(ssd1306_t *disp, const char *text)
{
    int width = (int)strlen(text) * font6x12_font.width;
    ssd1306_fill_rect(disp, 0, 0, SCREEN_WIDTH, 15);
    ssd1306_draw_str_black(disp, (SCREEN_WIDTH - width) / 2, 1, text, &font6x12_font);
}

/* Long durations retain the exact number of seconds in three rows. */
static void duration_hms(ssd1306_t *disp, uint32_t seconds, int y)
{
    unsigned long parts[] = {
        (unsigned long)(seconds / 3600),
        (unsigned long)((seconds / 60) % 60),
        (unsigned long)(seconds % 60),
    };
    static const char *units[] = {"h", "m", "s"};
    char number[32];
    if (parts[0] >= 100000) {
        snprintf(number, sizeof(number), "%lu", parts[0]);
        centered(disp, y, number, &font5x8_font);
        centered(disp, y + 10, "hours", &font5x8_font);
        snprintf(number, sizeof(number), "%02lum%02lus", parts[1], parts[2]);
        centered(disp, y + 20, number, &font5x8_font);
        return;
    }
    for (int row = 0; row < 3; ++row) {
        snprintf(number, sizeof(number), "%lu", parts[row]);
        int digits = (int)strlen(number);
        if (digits <= 3) {
            ssd1306_draw_str(disp, 24 - digits * 8, y + row * 14,
                             number, &font_terminus8x14_font);
            ssd1306_draw_str(disp, 26, y + row * 14 + 4, units[row], &font5x8_font);
        } else {
            /* Extremely long manual exposures still fit without truncation. */
            char line[32];
            snprintf(line, sizeof(line), "%lu%s", parts[row], units[row]);
            centered(disp, y + row * 14 + 3, line, &font5x8_font);
        }
    }
}

static void shutter(ssd1306_t *disp, const camera_state_t *state)
{
    char value[8];
    if (state->menu == MENU_BULB) {
        if (state->time_mode < 2) {
            centered(disp, 30, state->time_mode == 0 ? "B" : "T", &font_terminus8x14_font);
        } else if (state->time_mode <= 9999) {
            snprintf(value, sizeof(value), "%u", (unsigned)state->time_mode);
            centered(disp, 28, value, &font_terminus8x14_font);
            centered(disp, 47, "s", &font6x12_font);
        } else {
            duration_hms(disp, state->time_mode, 17);
        }
        return;
    }

    uint8_t speed = state->menu == MENU_AUTO
        ? (state->has_flash ? 13 : state->metering.auto_shutter_pos)
        : state->shutter_speed;
    const char *label = get_shutter_speed(speed);
    if (label[0] == '1' && label[1] == '/') {
        centered(disp, 17, "1/", &font5x8_font);
        size_t n = strlen(label + 2);
        char suffix = 0;
        if (n > 0 && (label[n + 1] == 'A' || label[n + 1] == 'B')) {
            suffix = label[n + 1];
            --n;
        }
        if (n >= sizeof(value)) n = sizeof(value) - 1;
        memcpy(value, label + 2, n);
        value[n] = '\0';
        centered(disp, 28, value, &font_terminus8x14_font);
        if (suffix) {
            char unit[] = {'s', ' ', suffix, '\0'};
            centered(disp, 47, unit, &font6x12_font);
        } else {
            centered(disp, 47, "s", &font6x12_font);
        }
    } else {
        size_t n = strlen(label);
        if (n && label[n - 1] == 's') --n;
        if (n >= sizeof(value)) n = sizeof(value) - 1;
        memcpy(value, label, n);
        value[n] = '\0';
        centered(disp, 28, value, &font_terminus8x14_font);
        centered(disp, 47, "s", &font6x12_font);
    }
}

void display_show_frame(const camera_state_t *state, ssd1306_t *disp)
{
    if (!state->if_display) return;
    ssd1306_clear(disp);
    if (state->menu == MENU_SETTINGS_START)
        display_show_menu(state, disp);
    else if (state->menu < MENU_SETTINGS_START)
        display_show_taking(state, disp);
    ssd1306_show(disp);
}

void display_show_taking(const camera_state_t *state, ssd1306_t *disp)
{
    const char *mode = state->menu == MENU_AUTO ? "AUTO" :
        state->menu == MENU_MANUAL ? "MAN" : state->time_mode == 0 ? "BULB" : "TIME";
    header(disp, mode);
    shutter(disp, state);
    ssd1306_draw_line(disp, 0, 61, 31, 61);

    if (state->has_flash) {
        ssd1306_fill_rect(disp, 0, 64, 32, 12);
        ssd1306_draw_str_black(disp, 1, 64, "FLASH", &font6x12_font);
    } else {
        centered(disp, 65, "F OFF", &font5x8_font);
    }
    centered(disp, 80, "ISO600", &font5x8_font);

    char ev[12];
    float v = state->metering.ev;
    if (!isfinite(v)) snprintf(ev, sizeof(ev), "EV--");
    else if (v > 99.9f) snprintf(ev, sizeof(ev), "EV HI");
    else if (v < -9.9f) snprintf(ev, sizeof(ev), "EV LO");
    else snprintf(ev, sizeof(ev), "EV%.1f", (double)v);
    /* Rounding 99.9 to 100.0 would exceed the available width. */
    if (strlen(ev) > 6) snprintf(ev, sizeof(ev), v < 0 ? "EV LO" : "EV HI");
    centered(disp, 95, ev, &font5x8_font);

    char status[12], timer[4], multi[4];
    if (state->self_timer_sec > 0)
        snprintf(timer, sizeof(timer), "%u", (unsigned)state->self_timer_sec);
    else snprintf(timer, sizeof(timer), "--");
    if (state->multi_exp_remain < 0) snprintf(multi, sizeof(multi), "-");
    else snprintf(multi, sizeof(multi), "%u", (unsigned)state->multi_exp_remain);
    snprintf(status, sizeof(status), "T%sM%s", timer, multi);
    centered(disp, 110, status, &font5x8_font);

    if (!state->wifi_enabled && !state->ble_enabled) {
        centered(disp, 120, "AIR", &font5x8_font);
    } else {
        if (state->wifi_enabled) ssd1306_fill_rect(disp, 5, 120, 10, 8);
        if (state->ble_enabled) ssd1306_fill_rect(disp, 17, 120, 10, 8);
        if (state->wifi_enabled) ssd1306_draw_str_black(disp, 5, 120, "W+", &font5x8_font);
        else ssd1306_draw_str(disp, 5, 120, "W-", &font5x8_font);
        if (state->ble_enabled) ssd1306_draw_str_black(disp, 17, 120, "B+", &font5x8_font);
        else ssd1306_draw_str(disp, 17, 120, "B-", &font5x8_font);
    }
}

void display_show_menu(const camera_state_t *state, ssd1306_t *disp)
{
    header(disp, "MENU");
    char value[12];
    if (menu_item_idx == 0) {
        centered(disp, 24, "Self", &font5x8_font);
        centered(disp, 34, "Timer", &font5x8_font);
        snprintf(value, sizeof(value), "%us", (unsigned)state->self_timer_sec);
        centered(disp, 55, value, &font6x12_font);
    } else if (menu_item_idx == 1) {
        centered(disp, 24, "Multi", &font5x8_font);
        centered(disp, 34, "Exp", &font5x8_font);
        snprintf(value, sizeof(value), "%d", (int)state->multi_exp_remain);
        centered(disp, 55, value, &font6x12_font);
    } else if (menu_item_idx == 2) {
        centered(disp, 24, "Flight", &font5x8_font);
        centered(disp, 55, state->flight_mode ? "ON" : "OFF", &font6x12_font);
    } else {
        centered(disp, 24, "IP", &font5x8_font);
        if (!state->ip_str[0]) {
            centered(disp, 44, "not", &font5x8_font);
            centered(disp, 54, "conn", &font5x8_font);
        } else {
            /* One octet per line, retaining dots to make the address unambiguous. */
            const volatile char *p = state->ip_str;
            for (int row = 0; row < 4 && *p; ++row) {
                size_t n = 0;
                while (*p && *p != '.' && n < 3) value[n++] = *p++;
                if (*p == '.') value[n++] = *p++;
                value[n] = '\0';
                centered(disp, 42 + row * 10, value, &font5x8_font);
            }
        }
    }
    if (menu_adjust && menu_item_idx < 3)
        centered(disp, 80, "EDIT", &font5x8_font);
}

void display_show_countdown(ssd1306_t *disp, int32_t seconds_remaining)
{
    if (!disp || !disp->buff || seconds_remaining < 0) return;
    ssd1306_clear(disp);
    if (seconds_remaining <= 9999) {
        char value[8];
        snprintf(value, sizeof(value), "%ld", (long)seconds_remaining);
        centered(disp, 50, value, &font_terminus8x14_font);
        centered(disp, 70, "s", &font6x12_font);
    } else {
        duration_hms(disp, (uint32_t)seconds_remaining, 49);
    }
    ssd1306_show(disp);
}
