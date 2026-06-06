/* ssd1306.h - SSD1306 OLED display driver (ESP-IDF esp_lcd backend) */

#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <driver/i2c.h>
#include "esp_lcd_types.h"
#include "font.h"

typedef struct {
    uint16_t width;
    uint16_t height;
    uint16_t pages;
    uint8_t *buff;
    size_t buff_size;
    esp_lcd_panel_handle_t panel;
    esp_lcd_panel_io_handle_t io;
} ssd1306_t;

bool ssd1306_init(ssd1306_t *dev, uint16_t width, uint16_t height,
                  uint8_t i2c_addr, i2c_port_t i2c_port, bool external_vcc);
void ssd1306_deinit(ssd1306_t *dev);
void ssd1306_power_off(ssd1306_t *dev);
void ssd1306_power_on(ssd1306_t *dev);
void ssd1306_clear(ssd1306_t *dev);
void ssd1306_invert(ssd1306_t *dev, uint8_t inv);
void ssd1306_show(ssd1306_t *dev);
void ssd1306_contrast(ssd1306_t *dev, uint8_t val);
void ssd1306_draw_pixel(ssd1306_t *dev, uint16_t x, uint16_t y);
void ssd1306_clear_pixel(ssd1306_t *dev, uint16_t x, uint16_t y);
void ssd1306_draw_line(ssd1306_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2);
void ssd1306_draw_rect(ssd1306_t *dev, int16_t x, int16_t y, uint16_t width, uint16_t height);
void ssd1306_draw_ellipse(ssd1306_t *dev, int16_t x_center, int16_t y_center, uint16_t r_horiz, uint16_t r_vert);
void ssd1306_draw_circle(ssd1306_t *dev, int16_t x_center, int16_t y_center, uint16_t r);
void ssd1306_fill_rect(ssd1306_t *dev, int16_t x, int16_t y, uint16_t width, uint16_t height);
void ssd1306_clear_rect(ssd1306_t *dev, int16_t x, int16_t y, uint16_t width, uint16_t height);
void ssd1306_draw_str(ssd1306_t *dev, int x, int y, const char *str, const ssd1306_font_t *font);

/** 黑字版 draw_str（白底上画黑点） */
void ssd1306_draw_str_black(ssd1306_t *dev, int x, int y, const char *str, const ssd1306_font_t *font);
void ssd1306_scroll_horiz(ssd1306_t *dev, bool right, uint8_t start_page, uint8_t end_page, uint8_t speed);
void ssd1306_scroll_horiz_stop(ssd1306_t *dev);
void ssd1306_scroll_row_vert(ssd1306_t *dev, bool down);
