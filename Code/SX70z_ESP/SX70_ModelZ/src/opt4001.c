/* opt4001.c - OPT4001 环境光传感器 I2C 驱动：读取 lux、自动量程 */

#include "opt4001.h"
#include <driver/i2c.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char *TAG = "OPT4001";

/* ======================== Hardware Configuration ======================== */

#define I2C_MASTER_NUM          I2C_NUM_0
#define OPT4001_I2C_ADDR        0x44

/* OPT4001 Register Addresses */
#define OPT4001_REG_RESULT_MSB  0x00
#define OPT4001_REG_RESULT_LSB  0x01
#define OPT4001_REG_CONFIG      0x0A
#define OPT4001_REG_FLAGS       0x0B
#define OPT4001_REG_DEVICE_ID   0x11

/* Configuration Register Bits */
#define OPT4001_CFG_QWAKE           (0x8000)
#define OPT4001_CFG_RANGE_AUTO      (0x3000)
#define OPT4001_CFG_CONV_800MS      (0x02C0)
#define OPT4001_CFG_MODE_CONTINUOUS (0x0030)
#define OPT4001_CFG_LATCH           (0x0008)
#define OPT4001_CFG_INT_POL         (0x0000)
#define OPT4001_CFG_FAULT_CNT2      (0x0001)

/* Expected Device ID (bits 11:0 of register 0x11) */
#define OPT4001_DEVICE_ID_MASK  0x3FFF
#define OPT4001_DEVICE_ID       0x0121

/* ======================== Static State ======================== */

static bool initialized = false;
static uint8_t last_range_index = 0;

/* ======================== I2C Communication ======================== */

static esp_err_t opt4001_write_reg(uint8_t reg, uint16_t data) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OPT4001_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, (data >> 8) & 0xFF, true);
    i2c_master_write_byte(cmd, data & 0xFF, true);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write error reg=0x%02X, data=0x%04X: %s", reg, data, esp_err_to_name(ret));
    }

    return ret;
}

static esp_err_t opt4001_read_reg(uint8_t reg, uint16_t *data) {
    uint8_t data_rd[2];
    esp_err_t ret;

    /*
     * 采用两次独立事务（数据手册标准时序）：
     *   S → DevAddr+W → RegAddr → P
     *   S → DevAddr+R → Data → P
     *
     * 备选方案：Repeated Start 合并为一次事务更快
     *   S → DevAddr+W → RegAddr → Sr → DevAddr+R → Data → P
     * 待验证 OPT4001 是否支持 Sr 后再切换。
     */

    /* Step 1: Write register address with STOP */
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OPT4001_I2C_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_stop(cmd);

    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C write reg error: %s", esp_err_to_name(ret));
        return ret;
    }

    /* Step 2: Read data with STOP */
    cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (OPT4001_I2C_ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data_rd[0], I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &data_rd[1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(100));
    i2c_cmd_link_delete(cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C read data error: %s", esp_err_to_name(ret));
        return ret;
    }

    *data = (data_rd[0] << 8) | data_rd[1];
    return ESP_OK;
}

/* ======================== I2C Scanner (诊断用) ======================== */

void opt4001_i2c_scan(void) {
    ESP_LOGI(TAG, "Scanning I2C0 (GPIO21=SCL, GPIO22=SDA)...");
    int found = 0;
    for (uint8_t addr = 0x01; addr < 0x7F; addr++) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(50));
        i2c_cmd_link_delete(cmd);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "  Device found at 0x%02X", addr);
            found++;
        }
    }
    if (found == 0) {
        ESP_LOGW(TAG, "  No devices found — check SDA/SCL wiring, pull-ups, and power");
    } else {
        ESP_LOGI(TAG, "  Scan complete: %d device(s) found", found);
    }
}

/* ======================== Public API ======================== */

esp_err_t opt4001_init(void) {
    /* 扫描 I2C0 总线，诊断硬件连接 */
    opt4001_i2c_scan();

    if (initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_OK;
    }

    uint16_t device_id;
    esp_err_t ret = opt4001_read_reg(OPT4001_REG_DEVICE_ID, &device_id);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read device ID");
        return ret;
    }

    device_id &= OPT4001_DEVICE_ID_MASK;
    if (device_id != OPT4001_DEVICE_ID) {
        ESP_LOGE(TAG, "Invalid device ID: 0x%03X (expected 0x%03X)", device_id, OPT4001_DEVICE_ID);
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG, "Device ID verified: 0x%03X", device_id);

    uint16_t config = OPT4001_CFG_RANGE_AUTO |
                    OPT4001_CFG_CONV_800MS |
                    OPT4001_CFG_MODE_CONTINUOUS |
                    OPT4001_CFG_LATCH |
                    OPT4001_CFG_INT_POL |
                    OPT4001_CFG_FAULT_CNT2;

    ret = opt4001_write_reg(OPT4001_REG_CONFIG, config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write configuration: %s", esp_err_to_name(ret));
        return ret;
    }

    vTaskDelay(pdMS_TO_TICKS(900));

    initialized = true;
    ESP_LOGI(TAG, "Initialized successfully (auto-range, 800ms, continuous)");

    return ESP_OK;
}

esp_err_t opt4001_read_lux(float *lux) {
    if (!initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (lux == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint16_t light_msb, light_lsb;
    esp_err_t ret = opt4001_read_reg(OPT4001_REG_RESULT_MSB, &light_msb);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = opt4001_read_reg(OPT4001_REG_RESULT_LSB, &light_lsb);
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t exponent = (light_msb >> 12) & 0x0F;
    uint16_t msb_data = light_msb & 0x0FFF;
    uint8_t lsb_data = (light_lsb >> 8) & 0xFF;
    uint32_t mantissa = (msb_data << 8) | lsb_data;

    last_range_index = exponent;

    *lux = (float)mantissa * (float)(1U << exponent) * 0.0004375f;

    ESP_LOGD(TAG, "MSB=0x%04X, LSB=0x%04X, exp=%u, mantissa=%lu, lux=%.1f",
             light_msb, light_lsb, exponent, mantissa, *lux);

    return ESP_OK;
}
