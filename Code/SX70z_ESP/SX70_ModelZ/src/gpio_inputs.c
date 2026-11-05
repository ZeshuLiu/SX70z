/* gpio_inputs.c - GPIO 输入处理：S1/S2 去抖、3D 按键、菜单/模式切换 */

#include "gpio_inputs.h"
#include "camera_main.h"
#include "flight_cfg.h"
#include "pcf8575.h"
#include "PIN.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "gpio";

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
static const int8_t self_timer_opts[] = {0, 2, 5, 10};

// 多重曝光选项（-1=中止, 0=正常, 1~10=额外张数）
static const int8_t multi_exp_opts[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
#define MULTI_EXP_OPTS_COUNT 12

// 飞行模式选项（0=关闭, 1=开启）
static const int8_t flight_opts[] = {0, 1};
#define FLIGHT_OPTS_COUNT 2

// 菜单参数项
typedef struct {
    const char *name;         // 显示名称
    uint8_t opts_count;       // 选项个数（0=只读，不响应调整）
    const int8_t *opts;       // 可选值表（NULL=只读）
    int8_t *value;            // 指向 camera_state 字段（只读项可为 NULL）
} menu_item_t;

static const menu_item_t menu_items[] = {
    {"SelfTimer", SELF_TIMER_OPTS_COUNT, (const int8_t *)self_timer_opts, (int8_t *)&camera_state.self_timer_sec},
    {"MultiExp",  MULTI_EXP_OPTS_COUNT,   multi_exp_opts,                (int8_t *)&camera_state.multi_exp_remain},
    {"Flight",    FLIGHT_OPTS_COUNT,      flight_opts,                   (int8_t *)&camera_state.flight_mode},
    {"IP Addr",   0,                       NULL,                          NULL},
};
#define MENU_ITEM_COUNT 4

int8_t menu_item_idx = 0;   // 当前选中的参数索引
bool menu_adjust = false;    // true=正在调值
static int8_t flight_mode_orig;  // 进入菜单时的飞行模式值，用于检测变更


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
        snprintf((char *)camera_state.cam_mode, sizeof(camera_state.cam_mode), "AUTO");
        camera_state.shut_mode = '1';
    } else if (camera_state.menu == MENU_BULB) {
        // menu 1: B/T 合并，通过 time_mode 区分
        if (camera_state.time_mode == 0) {
            snprintf((char *)camera_state.cam_mode, sizeof(camera_state.cam_mode), "B");
            camera_state.shut_mode = 'B';
        } else if (camera_state.time_mode == 1) {
            snprintf((char *)camera_state.cam_mode, sizeof(camera_state.cam_mode), "T");
            camera_state.shut_mode = 'T';
        } else {
            // time_mode >= 2: 秒值延时，暂用 T 标记
            snprintf((char *)camera_state.cam_mode, sizeof(camera_state.cam_mode),
                    "%us", camera_state.time_mode);
            camera_state.shut_mode = 'T';
        }
    } else if (camera_state.menu == MENU_MANUAL) {
        snprintf((char *)camera_state.cam_mode, sizeof(camera_state.cam_mode), "%s",
                get_shutter_speed(camera_state.shutter_speed));
        camera_state.shut_mode = '1';
    } else if (camera_state.menu == MENU_SELF_TIMER) {
        snprintf((char *)camera_state.cam_mode, sizeof(camera_state.cam_mode), "Menu 0");
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
        if (menu_adjust) {
            // 调整当前参数值
            const menu_item_t *item = &menu_items[menu_item_idx];
            if (item->opts == NULL) return;  // 只读项不可调整
            for (int i = 0; i < item->opts_count; i++) {
                if (item->opts[i] == *item->value) {
                    *item->value = item->opts[(i - 1 + item->opts_count) % item->opts_count];
                    break;
                }
            }
        } else {
            // 切换参数
            menu_item_idx = (menu_item_idx + 1) % MENU_ITEM_COUNT;
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
        if (menu_adjust) {
            const menu_item_t *item = &menu_items[menu_item_idx];
            if (item->opts == NULL) return;  // 只读项不可调整
            for (int i = 0; i < item->opts_count; i++) {
                if (item->opts[i] == *item->value) {
                    *item->value = item->opts[(i + 1) % item->opts_count];
                    break;
                }
            }
        } else {
            menu_item_idx = (menu_item_idx - 1 + MENU_ITEM_COUNT) % MENU_ITEM_COUNT;
        }
    }
    update_mode_display();
}

// 短按"按下"键：菜单循环 / 菜单内切换调整模式
static void push_button_short(void)
{
    if (camera_state.menu == MENU_SELF_TIMER) {
        // 只读项不进入调整模式
        if (menu_items[menu_item_idx].opts != NULL)
            menu_adjust = !menu_adjust;
    } else if (camera_state.menu < MENU_SETTINGS_START) {
        camera_state.menu = (camera_state.menu + 1) % MENU_COUNT;
    }
    update_mode_display();
}

// 长按"按下"键：进入/退出设置菜单
static void push_button_long(void)
{
    if (camera_state.menu < MENU_SETTINGS_START) {
        camera_state.menu = MENU_SELF_TIMER;
        menu_item_idx = 0;
        menu_adjust = false;
        flight_mode_orig = camera_state.flight_mode;
    } else {
        // 退出菜单前：飞行模式有变则保存到 NVS 并重启
        if (camera_state.flight_mode != flight_mode_orig) {
            esp_err_t err = flight_cfg_set(camera_state.flight_mode);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "Flight mode changed, restarting...");
                vTaskDelay(pdMS_TO_TICKS(1000));  // 1s 等待 NVS 操作完成
                esp_restart();
            } else {
                ESP_LOGE(TAG, "Failed to save flight mode: %d", err);
            }
        }
        camera_state.menu = MENU_AUTO;
        menu_adjust = false;
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
