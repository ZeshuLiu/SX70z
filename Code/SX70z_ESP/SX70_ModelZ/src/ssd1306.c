/* ssd1306.c - SSD1306 OLED display driver (ESP-IDF esp_lcd backend) */

#include <stdlib.h>
#include <string.h>
#include <esp_log.h>
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ssd1306.h"
#include "esp_lcd_io_i2c.h"
#include "ssd1306.h"

static const char *TAG = "SSD1306";

static void draw_pixel(ssd1306_t *dev, uint16_t x, uint16_t y, bool color) {
    if (x < dev->width && y < dev->height) {
        if (color) {
            dev->buff[x + dev->width * (y >> 3)] |= 0x01u << (y & 7);
        } else {
            dev->buff[x + dev->width * (y >> 3)] &= ~(0x01u << (y & 7));
        }
    }
}

static void fill_rect(ssd1306_t *dev, int16_t x_in, int16_t y_in,
                    uint16_t width, uint16_t height, bool color) {
    uint16_t x = x_in < 0 ? 0 : x_in;
    uint16_t y = y_in < 0 ? 0 : y_in;
    uint16_t x_end = x + width;
    uint16_t y_end = y + height;
    x_end = x_end > dev->width ? dev->width : x_end;
    y_end = y_end > dev->height ? dev->height : y_end;

    for (uint16_t i = x; i < x_end; ++i) {
        for (uint16_t j = y; j < y_end; ++j) {
            draw_pixel(dev, i, j, color);
        }
    }
}

static void draw_char(ssd1306_t *dev, uint16_t x, uint16_t y, const char *chr,
                      const ssd1306_font_t *font) {
    uint8_t bytes_per_col = (font->height + 7) / 8;
    uint8_t stride = font->width * bytes_per_col;
    for (uint8_t i = 0; i < font->width; i++) {
        for (uint8_t b = 0; b < bytes_per_col; b++) {
            uint8_t line = font->data[(*chr - font->first) * stride + i * bytes_per_col + b];
            for (uint8_t j = 0; j < 8 && (b * 8 + j) < font->height; j++) {
                draw_pixel(dev, x + i, y + b * 8 + j, (line & 0x01u));
                line >>= 1;
            }
        }
    }
}

bool ssd1306_init(ssd1306_t *dev, uint16_t width, uint16_t height,
                  uint8_t i2c_addr, i2c_port_t i2c_port, bool external_vcc) {
    dev->width = width;
    dev->height = height;
    dev->pages = height / 8;
    dev->buff_size = width * dev->pages;

    dev->buff = malloc(dev->buff_size);
    if (!dev->buff) {
        ESP_LOGE(TAG, "Failed to allocate framebuffer");
        return false;
    }

    // I2C panel IO (v1 legacy driver, compatible with i2c_driver_install)
    esp_lcd_panel_io_i2c_config_t io_cfg = {
        .dev_addr = i2c_addr,
        .control_phase_bytes = 1,
        .dc_bit_offset = 6,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .flags.dc_low_on_data = 0,
        .flags.disable_control_phase = 0,
        .scl_speed_hz = 0,          // v1 driver uses pre-configured bus speed
    };
    esp_err_t ret;

    ret = esp_lcd_new_panel_io_i2c_v1((uint32_t)i2c_port, &io_cfg, &dev->io);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "I2C panel IO init failed: %s", esp_err_to_name(ret));
        free(dev->buff);
        dev->buff = NULL;
        return false;
    }

    // SSD1306 panel
    esp_lcd_panel_ssd1306_config_t ssd1306_cfg = { .height = height };
    esp_lcd_panel_dev_config_t panel_cfg = {
        .reset_gpio_num = -1,
        .bits_per_pixel = 1,
        .vendor_config = &ssd1306_cfg,
    };
    ret = esp_lcd_new_panel_ssd1306(dev->io, &panel_cfg, &dev->panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Panel create failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_io_del(dev->io);
        free(dev->buff);
        dev->io = NULL;
        dev->buff = NULL;
        return false;
    }

    ret = esp_lcd_panel_reset(dev->panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Panel reset failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(dev->panel);
        esp_lcd_panel_io_del(dev->io);
        free(dev->buff);
        dev->panel = NULL;
        dev->io = NULL;
        dev->buff = NULL;
        return false;
    }

    ret = esp_lcd_panel_init(dev->panel);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Panel init failed: %s", esp_err_to_name(ret));
        esp_lcd_panel_del(dev->panel);
        esp_lcd_panel_io_del(dev->io);
        free(dev->buff);
        dev->panel = NULL;
        dev->io = NULL;
        dev->buff = NULL;
        return false;
    }

    esp_lcd_panel_mirror(dev->panel, true, true);
    esp_lcd_panel_disp_on_off(dev->panel, true);

    ESP_LOGI(TAG, "Initialized %ux%u (esp_lcd backend)", width, height);
    return true;
}

void ssd1306_deinit(ssd1306_t *dev) {
    if (dev->panel) {
        esp_lcd_panel_del(dev->panel);
        dev->panel = NULL;
    }
    if (dev->io) {
        esp_lcd_panel_io_del(dev->io);
        dev->io = NULL;
    }
    if (dev->buff) {
        free(dev->buff);
        dev->buff = NULL;
    }
}

void ssd1306_power_off(ssd1306_t *dev) {
    esp_lcd_panel_disp_on_off(dev->panel, false);
}

void ssd1306_power_on(ssd1306_t *dev) {
    esp_lcd_panel_disp_on_off(dev->panel, true);
}

void ssd1306_clear(ssd1306_t *dev) {
    memset(dev->buff, 0, dev->buff_size);
}

void ssd1306_invert(ssd1306_t *dev, uint8_t inv) {
    esp_lcd_panel_invert_color(dev->panel, inv);
}

void ssd1306_show(ssd1306_t *dev) {
    esp_lcd_panel_draw_bitmap(dev->panel, 0, 0, dev->width, dev->height, dev->buff);
}

void ssd1306_contrast(ssd1306_t *dev, uint8_t val) {
    // esp_lcd_ssd1306 doesn't expose contrast; use raw command
    // (acceptable for now; contrast default works fine)
}

void ssd1306_draw_pixel(ssd1306_t *dev, uint16_t x, uint16_t y) {
    draw_pixel(dev, x, y, 1);
}

void ssd1306_clear_pixel(ssd1306_t *dev, uint16_t x, uint16_t y) {
    draw_pixel(dev, x, y, 0);
}

void ssd1306_draw_line(ssd1306_t *dev, uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    int16_t D, dx, dy, step_x = 1, step_y = 1;

    dx = x2 - x1;
    dy = y2 - y1;
    if (dx < 0) { dx = -dx; step_x = -step_x; }
    if (dy < 0) { dy = -dy; step_y = -step_y; }

    draw_pixel(dev, x1, y1, 1);
    if (dy < dx) {
        D = dy * 2 - dx;
        while (x1 != x2) {
            x1 += step_x;
            if (D >= 0) { y1 += step_y; D -= 2 * dx; }
            D += 2 * dy;
            draw_pixel(dev, x1, y1, 1);
        }
    } else {
        D = dy - dx * 2;
        while (y1 != y2) {
            y1 += step_y;
            if (D <= 0) { x1 += step_x; D += 2 * dy; }
            D -= 2 * dx;
            draw_pixel(dev, x1, y1, 1);
        }
    }
}

void ssd1306_draw_rect(ssd1306_t *dev, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    for (int16_t i = x; i < x + (int16_t)width; i++) {
        draw_pixel(dev, i, y, 1);
        draw_pixel(dev, i, y + height - 1, 1);
    }
    for (int16_t i = y; i < y + (int16_t)height; i++) {
        draw_pixel(dev, x, i, 1);
        draw_pixel(dev, x + width - 1, i, 1);
    }
}

void ssd1306_draw_ellipse(ssd1306_t *dev, int16_t x_center, int16_t y_center,
                          uint16_t r_horiz, uint16_t r_vert) {
    if (!r_horiz || !r_vert) return;
    if ((x_center + (int16_t)r_horiz) < 0 || (x_center - (int16_t)r_horiz) >= dev->width ||
        (y_center + (int16_t)r_vert) < 0 || (y_center - (int16_t)r_vert) >= dev->height) return;

    int16_t x = 0;
    int16_t y = (int16_t)r_vert;
    float rx = (float)r_horiz, ry = (float)r_vert;
    float rx2 = rx * rx, ry2 = ry * ry;
    float two_rx2 = 2.0f * rx2, two_ry2 = 2.0f * ry2;
    float dx = 0.0f, dy = two_rx2 * (float)y;
    float d1 = ry2 - (rx2 * ry) + (0.25f * rx2);

    while (dx <= dy) {
        draw_pixel(dev, x_center + x, y_center + y, 1);
        draw_pixel(dev, x_center - x, y_center + y, 1);
        draw_pixel(dev, x_center + x, y_center - y, 1);
        draw_pixel(dev, x_center - x, y_center - y, 1);
        if (d1 < 0.0f) { x++; dx += two_ry2; d1 += dx + ry2; }
        else { x++; y--; dx += two_ry2; dy -= two_rx2; d1 += dx - dy + ry2; }
    }

    float d2 = (ry2 * (x + 0.5f) * (x + 0.5f)) + (rx2 * (y - 1.0f) * (y - 1.0f)) - (rx2 * ry2);
    while (y >= 0) {
        draw_pixel(dev, x_center + x, y_center + y, 1);
        draw_pixel(dev, x_center - x, y_center + y, 1);
        draw_pixel(dev, x_center + x, y_center - y, 1);
        draw_pixel(dev, x_center - x, y_center - y, 1);
        if (d2 > 0.0f) { y--; dy -= two_rx2; d2 += rx2 - dy; }
        else { y--; x++; dx += two_ry2; dy -= two_rx2; d2 += dx - dy + rx2; }
    }
}

void ssd1306_draw_circle(ssd1306_t *dev, int16_t x_center, int16_t y_center, uint16_t r) {
    ssd1306_draw_ellipse(dev, x_center, y_center, r, r);
}

void ssd1306_fill_rect(ssd1306_t *dev, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    fill_rect(dev, x, y, width, height, 1);
}

void ssd1306_clear_rect(ssd1306_t *dev, int16_t x, int16_t y, uint16_t width, uint16_t height) {
    fill_rect(dev, x, y, width, height, 0);
}

void ssd1306_draw_str(ssd1306_t *dev, int x, int y, const char *str, const ssd1306_font_t *font) {
    const uint8_t last = font->first + font->count;

    do {
        uint8_t ch = (uint8_t)*str;
        if (ch < font->first || ch >= last) {
            x += font->width;
            continue;
        }
        draw_char(dev, x, y, str, font);
        x += font->width;
    } while (*(++str));
}

void ssd1306_draw_str_black(ssd1306_t *dev, int x, int y, const char *str, const ssd1306_font_t *font) {
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

void ssd1306_scroll_horiz(ssd1306_t *dev, bool right, uint8_t start_page, uint8_t end_page, uint8_t speed) {
    // esp_lcd_ssd1306 doesn't expose scroll; commands would need raw I2C
}

void ssd1306_scroll_horiz_stop(ssd1306_t *dev) {
}

void ssd1306_scroll_row_vert(ssd1306_t *dev, bool down) {
    uint16_t width = dev->width;
    uint16_t pages = dev->height / 8;

    for (uint16_t col = 0; col < width; ++col) {
        uint8_t carry = 0;
        if (down) {
            for (uint16_t page = 0; page < pages; ++page) {
                uint16_t idx = page * width + col;
                uint8_t byte = dev->buff[idx];
                uint8_t new_carry = (byte & 0x80u) ? 0x01u : 0u;
                dev->buff[idx] = (uint8_t)((byte << 1) | carry);
                carry = new_carry;
            }
            dev->buff[col] = (dev->buff[col] & ~0x01u) | carry;
        } else {
            for (int page = (int)pages - 1; page >= 0; --page) {
                uint16_t idx = (uint16_t)page * width + col;
                uint8_t byte = dev->buff[idx];
                uint8_t new_carry = (byte & 0x01u) ? 0x80u : 0u;
                dev->buff[idx] = (uint8_t)((byte >> 1) | carry);
                carry = new_carry;
            }
            uint16_t last_idx = (pages - 1) * width + col;
            dev->buff[last_idx] = (dev->buff[last_idx] & ~0x80u) | carry;
        }
    }
}
