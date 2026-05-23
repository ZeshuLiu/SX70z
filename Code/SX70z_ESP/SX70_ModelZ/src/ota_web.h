#pragma once

#include "esp_err.h"

/** 启动 OTA Web 服务（HTTP 上传页面 + POST 固件升级），需 WiFi 已连接 */
esp_err_t ota_web_start(void);
