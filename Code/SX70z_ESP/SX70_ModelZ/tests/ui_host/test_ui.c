#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "display_manager.h"
#include "gpio_inputs.h"

int8_t menu_item_idx;
bool menu_adjust;
static const char *labels[] = {
    "3s", "2.5s", "2s", "1.5s", "1s", "1/2", "1/3", "1/4", "1/6",
    "1/8", "1/10", "1/15", "1/20", "1/30", "1/45", "1/60", "1/90",
    "1/125", "1/180", "1/250", "1/360", "1/500", "1/1000", "1/2000",
    "1/4000A", "1/4000B", "1/8000"
};
const char *get_shutter_speed(uint8_t i) { return labels[i < 27 ? i : 26]; }

static uint8_t storage[512 + 32];
static ssd1306_t d = {.width=128, .height=32, .panel_width=128,
                       .panel_height=32, .pages=4, .buff=storage+16, .buff_size=512};
static unsigned show_calls;
void esp_lcd_panel_draw_bitmap(void *panel, int x0, int y0, int x1, int y1, const void *buf) {
    (void)panel;
    assert(x0 == 0 && y0 == 0 && x1 == 128 && y1 == 32 && buf == d.buff);
    ++show_calls;
}

static void guard(void) {
    for (int i=0; i<16; ++i) {
        assert(storage[i] == 0xa5);
        assert(storage[528+i] == 0xa5);
    }
}
static unsigned count(void) {
    unsigned n=0;
    for (unsigned i=0; i<512; ++i)
        for (int b=0; b<8; ++b) n += (d.buff[i] >> b) & 1;
    return n;
}
static unsigned pixel(unsigned x, unsigned y) {
    return (d.buff[y + 128 * ((31-x) / 8)] >> ((31-x) % 8)) & 1;
}
static const char *output_dir;
static void dump(const char *name) {
    if (!output_dir) return;
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s.pbm", output_dir, name);
    FILE *f = fopen(path, "wb"); assert(f);
    fputs("P1\n32 128\n", f);
    for (unsigned y=0; y<128; ++y) {
        for (unsigned x=0; x<32; ++x) fprintf(f, "%u ", pixel(x,y));
        fputc('\n', f);
    }
    fclose(f);
}

int main(int argc, char **argv) {
    if (argc > 1) output_dir = argv[1];
    memset(storage, 0xa5, sizeof(storage));
    ssd1306_set_rotation(&d, SSD1306_ROTATION_90_CW);
    assert(d.width == 32 && d.height == 128 && count() == 0);
    /* Every logical point maps to exactly one distinct expected physical bit. */
    for (unsigned y=0; y<128; ++y) for (unsigned x=0; x<32; ++x) {
        ssd1306_draw_pixel(&d, x, y);
        assert(pixel(x,y));
    }
    assert(count() == 4096);
    ssd1306_draw_pixel(&d, 32, 127);
    ssd1306_draw_pixel(&d, 31, 128);
    guard();
    ssd1306_clear(&d);
    ssd1306_fill_rect(&d, -2, -2, 3, 3);
    assert(count() == 1 && pixel(0,0));
    ssd1306_clear(&d);
    ssd1306_draw_pixel(&d, 3, 127);
    ssd1306_scroll_row_vert(&d, true);
    assert(count() == 1 && pixel(3,0));
    ssd1306_scroll_row_vert(&d, false);
    assert(count() == 1 && pixel(3,127));

    camera_state_t state = {0};
    state.if_display = true;
    state.menu = MENU_MANUAL;
    state.metering.ev = 13.6f;
    uint8_t frames[27][512];
    for (unsigned i=0; i<27; ++i) {
        state.shutter_speed = i;
        display_show_frame(&state, &d);
        assert(count() > 0);
        char name[24]; snprintf(name, sizeof(name), "shutter_%02u", i); dump(name);
        memcpy(frames[i], d.buff, 512);
        for (unsigned j=0; j<i; ++j) assert(memcmp(frames[i], frames[j], 512));
        guard();
    }
    /* AUTO flash selects 1/30, while manual flash retains the chosen speed. */
    state.menu = MENU_AUTO; state.metering.auto_shutter_pos = 26; state.has_flash = true;
    display_show_frame(&state, &d);
    dump("auto_flash");
    uint8_t auto_flash[512]; memcpy(auto_flash, d.buff, 512);
    state.metering.auto_shutter_pos = 0; display_show_frame(&state, &d);
    assert(!memcmp(auto_flash, d.buff, 512));
    state.has_flash = false; display_show_frame(&state, &d);
    assert(memcmp(auto_flash, d.buff, 512));
    state.self_timer_sec = 10; state.multi_exp_remain = 10;
    uint8_t radios[4][512];
    for (int i=0; i<4; ++i) {
        state.wifi_enabled = (i & 1) != 0; state.ble_enabled = (i & 2) != 0;
        display_show_frame(&state, &d); memcpy(radios[i], d.buff, 512);
        char name[24]; snprintf(name, sizeof(name), "radio_%d", i); dump(name);
        for (int j=0; j<i; ++j) assert(memcmp(radios[i], radios[j], 512));
        guard();
    }
    state.multi_exp_remain = -1; display_show_frame(&state, &d); guard();
    state.menu = MENU_BULB;
    const int durations[] = {0,1,2,10,9999,10000,18000};
    for (unsigned i=0; i<sizeof(durations)/sizeof(durations[0]); ++i) {
        state.time_mode = durations[i]; display_show_frame(&state, &d);
        char name[24]; snprintf(name, sizeof(name), "time_%d", durations[i]); dump(name);
        assert(count() > 0); guard();
    }
    state.menu = MENU_SELF_TIMER;
    strcpy((char *)state.ip_str, "255.255.255.255");
    for (menu_item_idx=0; menu_item_idx<4; ++menu_item_idx) {
        for (int edit=0; edit<2; ++edit) {
            menu_adjust = edit; display_show_frame(&state, &d); assert(count() > 0); guard();
            char name[24]; snprintf(name, sizeof(name), "menu_%d_%d", menu_item_idx, edit); dump(name);
        }
    }
    const int counts[] = {0,1,10,9999,10000,18000,32767,32768};
    for (unsigned i=0; i<sizeof(counts)/sizeof(counts[0]); ++i) {
        display_show_countdown(&d, counts[i]); assert(count() > 0); guard();
        char name[24]; snprintf(name, sizeof(name), "count_%d", counts[i]); dump(name);
    }
    unsigned previous_shows = show_calls;
    ssd1306_t missing = {0};
    display_show_countdown(NULL, 10);
    display_show_countdown(&missing, 10);
    display_show_countdown(&d, -1);
    assert(show_calls == previous_shows);
    assert(show_calls > 27);
    puts("UI host drawing checks passed");
    return 0;
}
