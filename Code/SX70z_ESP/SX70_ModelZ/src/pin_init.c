/* pin_init.c - GPIO 引脚初始化：输出/输入/I2C 引脚配置 */

#include "pin_init.h"
#include "PIN.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"

static const char *TAG = "pin";

void pin_init(void)
{
    /* ---- 输出引脚：全部初始化为低电平 ---- */
    gpio_config_t out_cfg = {
        .pin_bit_mask = (1ULL << LED1_PIN)
                      | (1ULL << LED_TEST_PIN)
                      | (1ULL << SOL1_PIN)
                      | (1ULL << SOL2_PIN)
                      | (1ULL << MOTOR_PIN)
                      | (1ULL << FF_PIN),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&out_cfg);
    gpio_set_level(FF_PIN, 0);
    gpio_set_level(SOL1_PIN, 0);
    gpio_set_level(SOL2_PIN, 0);
    gpio_set_level(MOTOR_PIN, 0);
    gpio_set_level(LED1_PIN, 0);
    gpio_set_level(LED_TEST_PIN, 0);

    /* ---- 输入引脚 ---- */
    gpio_config_t in_cfg = {
        .pin_bit_mask = (1ULL << S1T_PIN)
                      | (1ULL << S3_PIN)
                      | (1ULL << S5_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&in_cfg);

    // S2: 闪光灯检测，低有效，芯片内部上拉
    gpio_config_t s2_cfg = {
        .pin_bit_mask = (1ULL << S2_PIN),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&s2_cfg);

    /* ---- I2C0 (PCF8575 等外设) ---- */
    i2c_config_t i2c0_cfg = {
        .mode          = I2C_MODE_MASTER,
        .sda_io_num    = SDA0_PIN,
        .scl_io_num    = SCL0_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_0, &i2c0_cfg);
    i2c_driver_install(I2C_NUM_0, I2C_MODE_MASTER, 0, 0, 0);

    /* ---- I2C1 (SSD1306 / PCF8575) ---- */
    i2c_config_t i2c1_cfg = {
        .mode          = I2C_MODE_MASTER,
        .sda_io_num    = SDA1_PIN,
        .scl_io_num    = SCL1_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = 100000,
    };
    i2c_param_config(I2C_NUM_1, &i2c1_cfg);
    i2c_driver_install(I2C_NUM_1, I2C_MODE_MASTER, 0, 0, 0);

    ESP_LOGI(TAG, "GPIO + I2C0 + I2C1 initialized");
}
