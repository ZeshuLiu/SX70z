#include "camera_main.h"
#include "display_manager.h"
#include "gpio_inputs.h"
#include "fonts/font5x8.h"
#include "opt4001.h"
#include "ssd1306.h"
#include "pcf8575.h"
#include "PIN.h"
#include "esp_log.h"
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_task_wdt.h"
#include <string.h>
#include <math.h>

static const char *TAG = "camera";

TaskHandle_t control_task_handle = NULL;
static TaskHandle_t display_task_handle = NULL;
static TaskHandle_t metering_task_handle = NULL;

ssd1306_t display;
pcf8575_t gpio_expander;

static TaskHandle_t shutter_task_handle = NULL;

/* ---- us 级精确定时器（GPTimer one-shot alarm + ISR, Core 1 同核） ---- */
static volatile bool g_timer_done;
static gptimer_handle_t g_delay_timer;

static bool IRAM_ATTR delay_timer_cb(gptimer_handle_t timer,
                                      const gptimer_alarm_event_data_t *edata,
                                      void *user_ctx)
{
    g_timer_done = true;
    return false;
}

static gptimer_alarm_config_t g_alarm_cfg = {
    .alarm_count = 0,
    .reload_count = 0,
    .flags.auto_reload_on_alarm = false,
};

void delay_us(uint32_t us)
{
    gptimer_stop(g_delay_timer);
    gptimer_set_raw_count(g_delay_timer, 0);
    g_alarm_cfg.alarm_count = us;
    gptimer_set_alarm_action(g_delay_timer, &g_alarm_cfg);
    g_timer_done = false;
    gptimer_start(g_delay_timer);
    while (!g_timer_done) {
        __asm__("nop");
    }
}

/* ---- LEDC PWM 电磁铁控制（100% 吸合 / 40% 保持） ---- */

static void sol1_pull(void)
{
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, SOL1_DUTY_FULL);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

static void sol1_hold(void)
{
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, SOL1_DUTY_HOLD);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

static void sol1_release(void)
{
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0, SOL2_DUTY_OFF);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_0);
}

static void sol2_engage(void)
{
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, SOL2_DUTY_ON);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
}

static void sol2_disengage(void)
{
    ledc_set_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1, SOL2_DUTY_OFF);
    ledc_update_duty(LEDC_HIGH_SPEED_MODE, LEDC_CHANNEL_1);
}

camera_state_t camera_state = {
    .if_display = false,
    .metering = {
        .last_lux = 0.0f,
        .auto_shutter_pos = 5,  // 1/2s，安全起步
    },
    .button = {
        .available = false,
        .old_value = "111",
        .debounce_last = 0,
        .push_down_start = 0,
    },
    .menu = 0,
    .cam_mode = "AUTO",
    .shut_mode = '1',
    .shutter_speed = 4,  // 1s
    .test_led_level = false,
    .has_flash = false,
    .self_timer_sec = 0,
};

/* ---- 快门速度表（移植自 SX70Mk2 metering.c） ---- */

static const char *shutter_speeds[] = {
    "3s", "2.5s", "2s", "1.5s", "1s",
    "1/2", "1/3", "1/4", "1/6", "1/8", "1/10", "1/15", "1/20",
    "1/30", "1/45", "1/60", "1/90", "1/125", "1/180", "1/250", "1/360",
    "1/500", "1/1000", "1/2000", "1/4000A", "1/4000B", "1/8000"
};

// 快门时间 (0.1ms 单位)，对应上表
static const uint16_t shutter_times_x10[] = {
    30000, 25000, 20000, 15000, 10500,
    5400, 4700, 3000, 2900, 1800, 1450, 1060, 880,
    690, 610, 540, 500, 450, 430, 410, 370, 340, 320,
    300, 280, 260, 240
};

const char *get_shutter_speed(uint8_t index)
{
    if (index >= SHUTTER_SPEED_COUNT) index = SHUTTER_SPEED_COUNT - 1;
    return shutter_speeds[index];
}

uint16_t get_shutter_time_x10(uint8_t index)
{
    if (index >= SHUTTER_SPEED_COUNT) index = SHUTTER_SPEED_COUNT - 1;
    return shutter_times_x10[index];
}

// 根据校准后 EV 计算快门速度 (F/8 镜头，标称快门)
//
// 原理: EV = log₂(F² / t) = log₂(64 / t_nominal)
// 阈值取相邻两档 EV 的中点
// 注: shutter_times_x10[] 为校准后实际延时，与此表无关
//
//  索引 | 快门    | 标称 t   | EV    | 阈值 EV
//  ─────┼─────────┼──────────┼───────┼────────
//   0   | 3s      | 3.0      |  4.42 | ≤4.55
//   1   | 2.5s    | 2.5      |  4.68 | ≤4.84
//   2   | 2s      | 2.0      |  5.00 | ≤5.21
//   3   | 1.5s    | 1.5      |  5.42 | ≤5.71
//   4   | 1s      | 1.0      |  6.00 | ≤6.50
//   5   | 1/2     | 0.5      |  7.00 | ≤7.29
//   6   | 1/3     | 0.333    |  7.58 | ≤7.79
//   7   | 1/4     | 0.25     |  8.00 | ≤8.29
//   8   | 1/6     | 0.167    |  8.58 | ≤8.79
//   9   | 1/8     | 0.125    |  9.00 | ≤9.16
//  10   | 1/10    | 0.1      |  9.32 | ≤9.62
//  11   | 1/15    | 0.0667   |  9.91 | ≤10.12
//  12   | 1/20    | 0.05     | 10.32 | ≤10.62
//  13   | 1/30    | 0.0333   | 10.91 | ≤11.20
//  14   | 1/45    | 0.0222   | 11.49 | ≤11.70
//  15   | 1/60    | 0.0167   | 11.91 | ≤12.20
//  16   | 1/90    | 0.0111   | 12.49 | ≤12.73
//  17   | 1/125   | 0.008    | 12.96 | ≤13.23
//  18   | 1/180   | 0.00556  | 13.49 | ≤13.73
//  19   | 1/250   | 0.004    | 13.96 | ≤14.23
//  20   | 1/360   | 0.00278  | 14.49 | ≤14.73
//  21   | 1/500   | 0.002    | 14.96 | ≤15.46
//  22   | 1/1000  | 0.001    | 15.96 | ≤16.46
//  23   | 1/2000  | 0.0005   | 16.96 | ≤17.46
//  24   | 1/4000A | 0.00025  | 17.96 | ≤18.46
//  25   | 1/4000B | 0.00025  | 17.96 | (手动)
//  26   | 1/8000  | 0.000125 | 18.96 | ——
//
// 返回快门速度索引 (0-26)
uint8_t calc_shutter_from_ev(float ev)
{
    if (ev <= 4.55f) return 0;   // 3s
    if (ev <= 4.84f) return 1;   // 2.5s
    if (ev <= 5.21f) return 2;   // 2s
    if (ev <= 5.71f) return 3;   // 1.5s
    if (ev <= 6.50f) return 4;   // 1s
    if (ev <= 7.29f) return 5;   // 1/2
    if (ev <= 7.79f) return 6;   // 1/3
    if (ev <= 8.29f) return 7;   // 1/4
    if (ev <= 8.79f) return 8;   // 1/6
    if (ev <= 9.16f) return 9;   // 1/8
    if (ev <= 9.62f) return 10;  // 1/10
    if (ev <= 10.12f) return 11; // 1/15
    if (ev <= 10.62f) return 12; // 1/20
    if (ev <= 11.20f) return 13; // 1/30
    if (ev <= 11.70f) return 14; // 1/45
    if (ev <= 12.20f) return 15; // 1/60
    if (ev <= 12.73f) return 16; // 1/90
    if (ev <= 13.23f) return 17; // 1/125
    if (ev <= 13.73f) return 18; // 1/180
    if (ev <= 14.23f) return 19; // 1/250
    if (ev <= 14.73f) return 20; // 1/360
    if (ev <= 15.46f) return 21; // 1/500
    if (ev <= 16.46f) return 22; // 1/1000
    if (ev <= 17.46f) return 23; // 1/2000
    if (ev <= 18.46f) return 24; // 1/4000A
    return 26;                    // 1/8000
}


/* ---- 显示任务（中优先级，200ms 周期，与按键/快门解耦） ---- */
static void display_task(void *pvParameters)
{
    while (1) {
        display_show_frame(&camera_state, &display);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

/* ---- 测光任务（低优先级，1s 周期） ---- */
static void metering_task(void *pvParameters)
{
    esp_err_t ret = opt4001_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OPT4001 init failed: %s", esp_err_to_name(ret));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "OPT4001 sensor initialized");

    static int opt_log_cnt = 0;
    while (1) {
        float lux;
        if (opt4001_read_lux(&lux) == ESP_OK) {
            camera_state.metering.last_lux_raw = lux;
            camera_state.metering.ev_raw = log2f(lux * 2.56f);  // 原始 EV
            lux *= METERING_ATTEN_K;          // 窗口衰减校准
            camera_state.metering.last_lux = lux;
            camera_state.metering.ev = log2f(lux * 2.56f);  // 校准后 EV
            camera_state.metering.auto_shutter_pos = calc_shutter_from_ev(camera_state.metering.ev);

            if (++opt_log_cnt % 5 == 0) {
                ESP_LOGI(TAG, "OPT4001: raw=%.1f cal=%.1f EV=%.1f, shutter=%s",
                        camera_state.metering.last_lux_raw, lux, camera_state.metering.ev,
                        get_shutter_speed(camera_state.metering.auto_shutter_pos));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

/* ---- 快门任务（移植 SX70Mk2 shutter_expose 完整时序） ---- */
static void shutter_task(void *pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        ESP_LOGI(TAG, "Taking picture...");

        // 检查是否在拍摄模式（menu 10 = 自拍定时）
        if (camera_state.menu >= 10) {
            ESP_LOGI(TAG, "Not in shooting mode (Manuel)");
            continue;
        }

#if HAS_FOCUS
        if (!camera_state.if_focused) {
            do_focus(&camera_state.shut_mode, &camera_state.shutter_speed);
        }
#else
        // TODO: 对焦功能待实现（需移植 do_focus / S1F_FBW_PIN）
#endif

        uint16_t shutter_delay_x10 = get_shutter_time_x10(camera_state.shutter_speed);
        char mode = camera_state.shut_mode;
        bool use_flash = camera_state.has_flash;

        // 关闭 LED
        gpio_set_level(LED1_PIN, 1);

        // ---- 1. 关闭快门 ----
        ESP_LOGD(TAG, "Shutter close");
        sol1_pull();                    // 100% 吸合
        delay_us(30000);                // 30ms
        sol1_hold();                    // 40% 保持
        ESP_LOGD(TAG, "Shutter closed");

        // ---- 2. 电机启动，反光板上升 ----
        gpio_set_level(MOTOR_PIN, 1);
        ESP_LOGI(TAG, "Motor start (mirror up)");

        // 等待反光板就位 (S3 变高, 5×7µs 防抖)
        while (!gpio_debounce_defaultLow(S3_PIN)) {
            delay_us(100);
        }
        gpio_set_level(MOTOR_PIN, 0);
        ESP_LOGI(TAG, "Motor stopped");

        // ---- 3. Y Delay ----
        if (camera_state.self_timer_sec > 0) {
            if (control_task_handle) vTaskSuspend(control_task_handle);
            if (display_task_handle) vTaskSuspend(display_task_handle);
            if (metering_task_handle) vTaskSuspend(metering_task_handle);
            for (int remain = camera_state.self_timer_sec; remain > 0; remain--) {
                display_show_countdown(&display, remain);
                delay_us(1000000);
            }
            if (metering_task_handle) vTaskResume(metering_task_handle);
            if (display_task_handle) vTaskResume(display_task_handle);
            if (control_task_handle) vTaskResume(control_task_handle);
        }

        if (use_flash) {  // SHUTTER_FLASH
            sol2_engage();
            ESP_LOGI(TAG, "Aperture engaged");
        }
        delay_us(18000);  // 18ms Y delay

        // ---- 4. 曝光 ----
        ESP_LOGI(TAG, "Exposure: mode=%c, delay=%d (0.1ms)", mode, shutter_delay_x10);

        if (mode == '1') {  // SHUTTER_NORMAL
            if(!use_flash){
                sol1_release();                               // shutter_open
                delay_us((uint32_t)shutter_delay_x10 * 100);  // 0.1ms → us
                // sol1_pull();                                  // 100% 吸合
            }
            else{
                int gap = (int)shutter_delay_x10 - 470;
                if (gap < 0) gap = 0;

                sol1_release();               // shutter_open
                delay_us(47000);              // 47ms

                gpio_set_level(FF_PIN, 1);     // 触发闪光灯
                delay_us(1000);                // 1ms
                gpio_set_level(FF_PIN, 0);
                delay_us((uint32_t)gap * 100); // 剩余延时

                // sol1_pull();                  // 100% 吸合
            }
            // delay_us(30000);                               // 30ms
            // sol1_hold();                                  // 40% 保持
        } else if (mode == 'B') {  // SHUTTER_BULB
            sol1_release();               // shutter_open
            delay_us(15000);              // 15ms

            if (use_flash){
                delay_us(32000);
                gpio_set_level(FF_PIN, 1);     // 触发闪光灯
                delay_us(1000);                // 1ms
                gpio_set_level(FF_PIN, 0);
            }

            // 等待 S1T 释放
            while (gpio_get_level(S1T_PIN) == 0) {
                delay_us(3000);
            }

        } else if (mode == 'T') {  // SHUTTER_TIME
            sol1_release();               // shutter_open

            if (use_flash){
                delay_us(47000);
                gpio_set_level(FF_PIN, 1);     // 触发闪光灯
                delay_us(1000);                // 1ms
                gpio_set_level(FF_PIN, 0);
            }

            // 等待 S1T 释放
            while (gpio_get_level(S1T_PIN) == 0) {
                delay_us(100);
            }
            // 等待 S1T 再次按下
            while (gpio_get_level(S1T_PIN) != 0) {
                delay_us(3000);
            }
        }

        // ---- 5. 关闭快门，曝光结束 ----
        sol1_pull();                    // 100% 吸合
        ESP_LOGD(TAG, "Shutter closing");
        delay_us(30000);                // 30ms
        sol1_hold();                    // 40% 保持
        delay_us(18000);                // 18ms

        // ---- 6. 光圈归位 ----
        if (use_flash) {  // SHUTTER_FLASH
            sol2_disengage();
            ESP_LOGI(TAG, "Aperture disengaged");
        }

        // ---- 7. 电机启动吐片 ----
        gpio_set_level(MOTOR_PIN, 1);
        ESP_LOGI(TAG, "Motor start (film ejection)");

        // 等待 S5 变低（胶片检测, 5×7µs 防抖）
        while (gpio_debounce_defaultHigh(S5_PIN)) {
            delay_us(100);
        }

        gpio_set_level(MOTOR_PIN, 0);
        sol1_release();               // shutter_open
        ESP_LOGI(TAG, "Film ejection complete");

        // ---- 8. 等待 S1T 释放，防止连拍 ----
        while (gpio_get_level(S1T_PIN) == 0) {
            delay_us(10000);  // 10ms
        }
    }
}

void control_task(void *pvParameters)
{
    /* ---- SSD1306 OLED（I2C1, 0x3C, 128x64） ---- */
    if (ssd1306_init(&display, 128, 32, 0x3C, I2C_NUM_1, false)) {
        ssd1306_clear(&display);
        ssd1306_draw_str(&display, 0, 0, "SX70z Ready", &font5x8_font);
        ssd1306_show(&display);
        camera_state.if_display = true;
        ESP_LOGI(TAG, "SSD1306 initialized");
        /* ---- 启动显示任务（Core 1，中优先级） ---- */
        xTaskCreatePinnedToCore(display_task, "display", 2048, NULL,
                                DISPLAY_TASK_PRIO, &display_task_handle, 1);
    } else {
        ESP_LOGE(TAG, "SSD1306 init failed");
    }

    /* ---- PCF8575 GPIO 扩展（I2C1, 0x20） ---- */
    if (pcf8575_init(&gpio_expander, I2C_NUM_1, 0x20, 0xFFFF) == 0) {
        ESP_LOGI(TAG, "PCF8575 initialized");
        camera_state.button.available = true;
    } else {
        ESP_LOGE(TAG, "PCF8575 init failed");
        camera_state.button.available = false;
    }

    /* ---- 启动测光任务（Core 1，低优先级） ---- */
    xTaskCreatePinnedToCore(metering_task, "metering", 2048, NULL,
                            METERING_TASK_PRIO, &metering_task_handle, 1);

    /* ---- 初始化 us 级精确定时器（GPTimer，ISR 绑定 Core 1） ---- */
    gptimer_config_t tcfg = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,  // 1MHz → 1μs
        .intr_priority = 1,
    };
    gptimer_new_timer(&tcfg, &g_delay_timer);

    gptimer_event_callbacks_t cbs = {
        .on_alarm = delay_timer_cb,
    };
    gptimer_register_event_callbacks(g_delay_timer, &cbs, NULL);
    gptimer_enable(g_delay_timer);

    /* ---- 初始化 LEDC PWM（SOL1: 快门电磁铁, SOL2: 光圈电磁铁, 8-bit 31.25kHz） ---- */
    ledc_timer_config_t ledc_timer = {
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = 31250,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t sol1_ch = {
        .gpio_num   = SOL1_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel    = LEDC_CHANNEL_0,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&sol1_ch);

    ledc_channel_config_t sol2_ch = {
        .gpio_num   = SOL2_PIN,
        .speed_mode = LEDC_HIGH_SPEED_MODE,
        .channel    = LEDC_CHANNEL_1,
        .timer_sel  = LEDC_TIMER_0,
        .duty       = 0,
        .hpoint     = 0,
    };
    ledc_channel_config(&sol2_ch);
    ESP_LOGI(TAG, "LEDC PWM initialized (31.25kHz, 8-bit)");

    /* ---- 启动快门任务（Core 1，最高优先级，平时阻塞） ---- */
    xTaskCreatePinnedToCore(shutter_task, "shutter", 2048, NULL,
                            SHUTTER_TASK_PRIO, &shutter_task_handle, 1);

    while (1) {
        // 3D 按键处理
        button3d_handler();
        update_mode_display();

        // 闪光灯检测
        camera_state.has_flash = gpio_inputs_read_s2();

        // S1 去抖读取
        bool s1t_pressed = debounce_read_s1pin();

        // S1T 全按快门 → 触发快门任务（参考 if s1t_pressed == 1）
        if (! s1t_pressed) {
            if (camera_state.menu < 10) {
                // AUTO 模式：自动选择测光计算的快门速度
                if (camera_state.menu == 0) {
                    camera_state.shutter_speed = camera_state.metering.auto_shutter_pos;
                    // 闪光灯同步：有闪光灯时固定 1/30s
                    if (camera_state.has_flash) {
                        camera_state.shutter_speed = 13;  // 1/30s
                    }
                }
                xTaskNotifyGive(shutter_task_handle);
            }
        }

        static int ctrl_log_cnt = 0;
        if (++ctrl_log_cnt % 16 == 0) {
            ESP_LOGI(TAG, "S1T=%d Flash=%d Mode=%s LUX=%.2f",
                    s1t_pressed, camera_state.has_flash,
                    camera_state.cam_mode, camera_state.metering.last_lux);
        }

        // 无屏幕时 LED 心跳指示，有屏幕则保持熄灭
        if (!camera_state.if_display) {
            camera_state.test_led_level = !camera_state.test_led_level;
            gpio_set_level(LED1_PIN, camera_state.test_led_level);
        } else {
            gpio_set_level(LED1_PIN, 1);  // LED 低电平点亮，1=灭
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}

void camera_pause(void)
{
    if (control_task_handle) {
        vTaskSuspend(control_task_handle);
        // 等待 Core 1 任务真正挂起后才返回
        while (eTaskGetState(control_task_handle) != eSuspended) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        ESP_LOGI(TAG, "Paused for OTA");
    }
}

void camera_resume(void)
{
    if (control_task_handle) {
        vTaskResume(control_task_handle);
        ESP_LOGI(TAG, "Resumed after OTA");
    }
}
