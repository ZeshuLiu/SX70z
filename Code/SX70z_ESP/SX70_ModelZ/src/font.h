/* font.h - Font type definition for SSD1306 */

#pragma once

#include <stdint.h>

typedef struct {
    const uint8_t *data;
    uint8_t width;
    uint8_t height;
    uint8_t first;
    uint8_t count;
} ssd1306_font_t;
