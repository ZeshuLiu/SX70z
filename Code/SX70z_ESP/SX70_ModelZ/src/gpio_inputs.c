/* gpio_inputs.c - GPIO 输入处理：S1/S2 去抖、3D 按键、菜单/模式切换 */

#include "gpio_inputs.h"
#include "camera_main.h"
#include "pcf8575.h"
#include "PIN.h"
#include "driver/gpio.h"
#include <stdio.h>
#include <string.h>

extern pcf8575_t gpio_expander;

/* ---- GPIO 防抖 ---- */

bool gpio_debounce_defaultLow(int pin) {
    for (int i = 0; i < GPIO_DEBOUNCE_COUNT; i++) {
        if (!gpio_get_level(pin)) return false;
        delay_us(7);
    }
    return true;
}

bool gpio_debounce_defaultHigh(int pin) {
    for (int i = 0; i < GPIO_DEBOUNCE_COUNT; i++) {
        if (gpio_get_level(pin)) return true;
        delay_us(7);
    }
    return false;
}

/* ---- S1 按键去抖 ---- */
static uint8_t s1f = 0;
static uint8_t s1t = 0;

bool debounce_read_s1pin(void)
{
    if (gpio_get_level(S1T_PIN) == 0) {
        s1t = (s1t == 0) ? s1t : s1t - 1;
    } else {
        s1t = S1_DEBOUNCE_COUNT;
    }
    // S1F 半按对焦——当前硬件可能没有，预留
    return s1t > 0;
}

/* ---- S2 闪光灯检测 ---- */
bool gpio_inputs_read_s2(void)
{
    static int flash_debounce = 0;
    bool flash_level = (gpio_get_level(S2_PIN) == 0);
    if (flash_level) {
        flash_debounce = (flash_debounce < 10) ? flash_debounce + 1 : 10;
    } else {
        flash_debounce = (flash_debounce > 0) ? flash_debounce - 1 : 0;
    }
    return flash_debounce >= 8;
}

/* ---- 3D 按键处理（移植自 SX70Mk2 主循环 button3d_handler） ---- */

// 自拍定时选项：0/2/5/10s
static const uint8_t self_timer_opts[] = {0, 2, 5, 10};


// 读取 PCF8575 按键状态 -> "101" 格式（1=未按下，0=按下）
static void read_3d_button_pins(char *result)
{
    if (!camera_state.button.available) {
        result[0] = '1'; result[1] = '1'; result[2] = '1'; result[3] = '\0';
        return;
    }
    uint16_t state = pcf8575_read(&gpio_expander);

    result[0] = ((state >> PCF_BUTTON3D_DOWN) & 1) ? '1' : '0';
    result[1] = ((state >> PCF_BUTTON3D_UP) & 1) ? '1' : '0';
    result[2] = ((state >> PCF_BUTTON3D_PUSH) & 1) ? '1' : '0';
    result[3] = '\0';
}

// 更新 cam_mode 字符串 + shut_mode
void update_mode_display(void)
{
    if (camera_state.menu == MENU_AUTO) {
        snprintf(camera_state.cam_mode, sizeof(camera_state.cam_mode), "AUTO");
        camera_state.shut_mode = '1';
    } else if (camera_state.menu == MENU_BULB) {
        // menu 1: B/T 合并，通过 time_mode 区分
        if (camera_state.time_mode == 0) {
            snprintf(camera_state.cam_mode, sizeof(camera_state.cam_mode), "B");
            camera_state.shut_mode = 'B';
        } else if (camera_state.time_mode == 1) {
            snprintf(camera_state.cam_mode, sizeof(camera_state.cam_mode), "T");
            camera_state.shut_mode = 'T';
        } else {
            // time_mode >= 2: 秒值延时，暂用 T 标记
            snprintf(camera_state.cam_mode, sizeof(camera_state.cam_mode),
                    "%us", camera_state.time_mode);
            camera_state.shut_mode = 'T';
        }
    } else if (camera_state.menu == MENU_MANUAL) {
        snprintf(camera_state.cam_mode, sizeof(camera_state.cam_mode), "%s",
                get_shutter_speed(camera_state.shutter_speed));
        camera_state.shut_mode = '1';
    } else if (camera_state.menu == MENU_SELF_TIMER) {
        snprintf(camera_state.cam_mode, sizeof(camera_state.cam_mode), "Menu 0");
    }
}

// 下键按下

// time_mode 步进（上键方向）：0→1, ≤10→+1, ≤60→+5, ≤600→+30, ≤1800→+60,
// ≤3600→+5min, ≤7200→+10min, 之后→+30min
static uint16_t time_mode_next(uint16_t cur)
{
    uint16_t step;
    if (cur < 10)   step = 1;
    else if (cur < 60)   step = 5;
    else if (cur < 600)  step = 30;
    else if (cur < 1800) step = 60;
    else if (cur < 3600) step = 300;
    else if (cur < 7200) step = 600;
    else step = 1800;

    uint16_t next = cur + step;
    if (next > 18000) return 0;  // 5h 上限，超过回绕到 0
    return next;
}

// time_mode 步进（下键方向，反向）
static uint16_t time_mode_prev(uint16_t cur)
{
    if (cur <= 0)   return 0;
    if (cur == 1)   return 0;
    if (cur <= 10)  return cur - 1;
    if (cur <= 60)  return cur - 5;
    if (cur <= 600) return cur - 30;
    if (cur <= 1800) return cur - 60;
    if (cur <= 3600) return cur - 300;
    if (cur <= 7200) return cur - 600;
    return cur - 1800;
}
static void down_button_call(void)
{
    if (camera_state.menu == MENU_BULB) {
        camera_state.time_mode = time_mode_prev(camera_state.time_mode);
    } else if (camera_state.menu == MENU_MANUAL) {
        if (camera_state.shutter_speed == 0) {
            camera_state.shutter_speed = SHUTTER_SPEED_COUNT - 1;
        } else {
            camera_state.shutter_speed--;
        }
    } else if (camera_state.menu == MENU_SELF_TIMER) {
        for (int i = 0; i < SELF_TIMER_OPTS_COUNT; i++) {
            if (self_timer_opts[i] == camera_state.self_timer_sec) {
                camera_state.self_timer_sec = self_timer_opts[(i - 1 + SELF_TIMER_OPTS_COUNT) % SELF_TIMER_OPTS_COUNT];
                break;
            }
        }
    }
    update_mode_display();
}

// 上键按下
static void up_button_call(void)
{
    if (camera_state.menu == MENU_BULB) {
        camera_state.time_mode = time_mode_next(camera_state.time_mode);
    } else if (camera_state.menu == MENU_MANUAL) {
        camera_state.shutter_speed = (camera_state.shutter_speed + 1) % SHUTTER_SPEED_COUNT;
    } else if (camera_state.menu == MENU_SELF_TIMER) {
        for (int i = 0; i < SELF_TIMER_OPTS_COUNT; i++) {
            if (self_timer_opts[i] == camera_state.self_timer_sec) {
                camera_state.self_timer_sec = self_timer_opts[(i + 1) % SELF_TIMER_OPTS_COUNT];
                break;
            }
        }
    }
    update_mode_display();
}

// 短按"按下"键：菜单循环 AUTO→BULB→TIME→MANUAL→AUTO
static void push_button_short(void)
{
    if(camera_state.menu < 10) camera_state.menu = (camera_state.menu + 1) % MENU_COUNT;
    update_mode_display();
}

// 长按"按下"键：进入/退出自拍定时
static void push_button_long(void)
{
    if (camera_state.menu < MENU_SETTINGS_START) {
        camera_state.menu = MENU_SELF_TIMER;
    } else {
        camera_state.menu = MENU_AUTO;
    }
    update_mode_display();
}

// 3D 按键处理（100ms 防抖，下降/上升沿检测，长短按区分）
void button3d_handler(void)
{
    if (!camera_state.button.available) return;

    char bt[4];
    read_3d_button_pins(bt);

    // 100ms 防抖
    uint32_t now = xTaskGetTickCount() * portTICK_PERIOD_MS;
    if (now - camera_state.button.debounce_last < 100) {
        return;
    }

    // 下键：下降沿触发 ('1'→'0')
    if (camera_state.button.old_value[0] == '1' && bt[0] == '0') {
        down_button_call();
    }

    // 上键：下降沿触发
    if (camera_state.button.old_value[1] == '1' && bt[1] == '0') {
        up_button_call();
    }

    // 按下键：记录按下时间
    if (camera_state.button.old_value[2] == '1' && bt[2] == '0') {
        camera_state.button.push_down_start = now;
    }

    // 按下键：松开时判断长短按
    if (camera_state.button.old_value[2] == '0' && bt[2] == '1') {
        uint32_t duration = now - camera_state.button.push_down_start;
        if (duration > 1000) {
            push_button_long();
        } else {
            push_button_short();
        }
    }

    // 保存状态
    camera_state.button.old_value[0] = bt[0];
    camera_state.button.old_value[1] = bt[1];
    camera_state.button.old_value[2] = bt[2];
    camera_state.button.old_value[3] = '\0';
    camera_state.button.debounce_last = now;
}
