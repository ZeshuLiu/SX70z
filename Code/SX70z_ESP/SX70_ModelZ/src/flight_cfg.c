/* flight_cfg.c — 飞行模式 NVS 持久化 */

#include "flight_cfg.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "flight_cfg";

#define FLIGHT_CFG_NVS_NS    "cfg"
#define FLIGHT_CFG_NVS_KEY   "flight"

static int8_t flight_mode = 0;

esp_err_t flight_cfg_init(void)
{
    nvs_handle_t h;
    esp_err_t err = nvs_open(FLIGHT_CFG_NVS_NS, NVS_READONLY, &h);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "No flight config in NVS, default OFF");
        flight_mode = 0;
        return ESP_OK;
    }

    int8_t val = 0;
    err = nvs_get_i8(h, FLIGHT_CFG_NVS_KEY, &val);
    nvs_close(h);

    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Flight key not found, default OFF");
        flight_mode = 0;
        return ESP_OK;
    }

    flight_mode = val ? 1 : 0;
    ESP_LOGI(TAG, "Flight mode: %s", flight_mode ? "ON" : "OFF");
    return ESP_OK;
}

int8_t flight_cfg_get(void)
{
    return flight_mode;
}

esp_err_t flight_cfg_set(int8_t mode)
{
    flight_mode = mode ? 1 : 0;

    nvs_handle_t h;
    esp_err_t err = nvs_open(FLIGHT_CFG_NVS_NS, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %d", err);
        return err;
    }

    err = nvs_set_i8(h, FLIGHT_CFG_NVS_KEY, flight_mode);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_set_i8 failed: %d", err);
        nvs_close(h);
        return err;
    }

    err = nvs_commit(h);
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Flight mode saved: %s", flight_mode ? "ON" : "OFF");
    }
    return err;
}
