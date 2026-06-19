/**
 * shutter_cal.c — 快门校准值 NVS 持久化
 *
 * 将 shutter_times_x10[] 从编译时常量改为运行时可写，
 * 首次启动从 NVS 加载；NVS 无数据或 magic 不匹配时回退出厂默认值。
 */

#include "shutter_cal.h"
#include <string.h>
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "shutter_cal";

/* ---- 互斥锁保护校准数据 ---- */
static SemaphoreHandle_t cal_mutex = NULL;

/* ---- NVS 配置 ---- */
#define NVS_NAMESPACE   "cal"
#define NVS_KEY         "shutter"
#define CAL_MAGIC       ((uint32_t)(0x53583000 | SHUTTER_SPEED_COUNT))

/* ---- 出厂默认值 (0.1ms) ---- */
static const uint16_t shutter_cal_default[SHUTTER_SPEED_COUNT] = {
    30000, 25000, 20000, 15000, 10500,
    5400, 4700, 3000, 2900, 1800, 1450, 1060, 880,
    690, 610, 540, 500, 450, 430, 410, 370, 340, 320,
    300, 280, 260, 240
};

/* ---- 快门速度标签 ---- */
static const char *shutter_speed_labels[SHUTTER_SPEED_COUNT] = {
    "3s", "2.5s", "2s", "1.5s", "1s",
    "1/2", "1/3", "1/4", "1/6", "1/8", "1/10", "1/15", "1/20",
    "1/30", "1/45", "1/60", "1/90", "1/125", "1/180", "1/250", "1/360",
    "1/500", "1/1000", "1/2000", "1/4000A", "1/4000B", "1/8000"
};

/* ---- 运行时校准数组 ---- */
static uint16_t shutter_cal_x10[SHUTTER_SPEED_COUNT];
static bool cal_is_valid = false;

/* ---- NVS blob 结构 ---- */
typedef struct {
    uint32_t magic;
    uint16_t values[SHUTTER_SPEED_COUNT];
} shutter_cal_blob_t;

esp_err_t shutter_cal_init(void)
{
    /* 创建互斥锁 */
    if (cal_mutex == NULL) {
        cal_mutex = xSemaphoreCreateMutex();
        if (cal_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace '%s' not found, using defaults", NVS_NAMESPACE);
        memcpy(shutter_cal_x10, shutter_cal_default, sizeof(shutter_cal_x10));
        cal_is_valid = false;
        return ESP_OK;
    }

    shutter_cal_blob_t blob;
    size_t blob_size = sizeof(blob);
    err = nvs_get_blob(h, NVS_KEY, &blob, &blob_size);
    nvs_close(h);

    if (err != ESP_OK || blob_size != sizeof(blob) || blob.magic != CAL_MAGIC) {
        if (err == ESP_OK && blob_size != sizeof(blob)) {
            ESP_LOGW(TAG, "Blob size mismatch (%zu != %zu), using defaults",
                     blob_size, sizeof(blob));
        } else if (err == ESP_OK && blob.magic != CAL_MAGIC) {
            ESP_LOGW(TAG, "Magic mismatch (0x%08x != 0x%08x), using defaults",
                     blob.magic, CAL_MAGIC);
        } else {
            ESP_LOGI(TAG, "No calibration data in NVS, using defaults");
        }
        memcpy(shutter_cal_x10, shutter_cal_default, sizeof(shutter_cal_x10));
        cal_is_valid = false;
        return ESP_OK;
    }

    memcpy(shutter_cal_x10, blob.values, sizeof(shutter_cal_x10));
    cal_is_valid = true;
    ESP_LOGI(TAG, "Loaded calibration from NVS (magic=0x%08x)", blob.magic);
    return ESP_OK;
}

esp_err_t shutter_cal_save(void)
{
    shutter_cal_blob_t blob = {
        .magic = CAL_MAGIC,
    };
    memcpy(blob.values, shutter_cal_x10, sizeof(blob.values));

    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %d", err);
        return err;
    }

    err = nvs_set_blob(h, NVS_KEY, &blob, sizeof(blob));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_blob failed: %d", err);
        nvs_close(h);
        return err;
    }

    err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration saved to NVS");
    }
    return err;
}

void shutter_cal_reset_defaults(void)
{
    if (cal_mutex != NULL) {
        xSemaphoreTake(cal_mutex, portMAX_DELAY);
    }
    memcpy(shutter_cal_x10, shutter_cal_default, sizeof(shutter_cal_x10));
    cal_is_valid = false;
    if (cal_mutex != NULL) {
        xSemaphoreGive(cal_mutex);
    }
}

bool shutter_cal_is_valid(void)
{
    return cal_is_valid;
}

void shutter_cal_set_valid(bool valid)
{
    cal_is_valid = valid;
}

uint16_t get_shutter_time_x10(uint8_t index)
{
    if (index >= SHUTTER_SPEED_COUNT) index = SHUTTER_SPEED_COUNT - 1;
    uint16_t value;
    if (cal_mutex != NULL) {
        xSemaphoreTake(cal_mutex, portMAX_DELAY);
    }
    value = shutter_cal_x10[index];
    if (cal_mutex != NULL) {
        xSemaphoreGive(cal_mutex);
    }
    return value;
}

const char *get_shutter_speed(uint8_t index)
{
    if (index >= SHUTTER_SPEED_COUNT) index = SHUTTER_SPEED_COUNT - 1;
    return shutter_speed_labels[index];
}

uint16_t *shutter_cal_get_array(void)
{
    if (cal_mutex != NULL) {
        xSemaphoreTake(cal_mutex, portMAX_DELAY);
    }
    return shutter_cal_x10;
}

void shutter_cal_unlock(void)
{
    if (cal_mutex != NULL) {
        xSemaphoreGive(cal_mutex);
    }
}

esp_err_t shutter_cal_erase(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed for erase: %d", err);
        return err;
    }

    err = nvs_erase_key(h, NVS_KEY);
    /* 如果键不存在，nvs_erase_key 返回 ESP_OK，忽略其他错误 */
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_erase_key returned: %d (key may not exist)", err);
    }

    err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Calibration erased from NVS");
    }
    return err;
}
