/**
 * OTA Web — HTTP 网页上传固件升级
 *
 * 连上 WiFi 后启动，浏览器打开 http://<ESP32_IP> 即可上传 .bin 升级。
 * 局域网内手机或电脑直接操作，无需外网服务器。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "esp_log.h"
#include "esp_http_server.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "camera_main.h"
#include "shutter_cal.h"
#include "devinfo.h"

static const char *TAG = "ota_web";

/* ================================================================
 *  HTML 上传页面（模板，%s 占位会被替换为设备信息）
 * ================================================================ */

#define HTML_HEAD \
    "<!DOCTYPE html><html><head><meta charset='utf-8'>" \
    "<meta name='viewport' content='width=device-width,initial-scale=1'>" \
    "<title>SX70z OTA</title>" \
    "<style>" \
    "body{font-family:sans-serif;max-width:400px;margin:40px auto;padding:0 20px}" \
    "table{width:100%%;border-collapse:collapse;margin:12px 0}" \
    "td{padding:4px 8px;border:1px solid #ddd}" \
    "td:first-child{background:#f5f5f5;font-weight:bold;width:40%%}" \
    "h2{color:#333}input,button{font-size:16px;padding:8px 12px;margin:8px 0}" \
    "#bar{width:100%%;background:#eee;height:20px;border-radius:4px;overflow:hidden;margin-top:12px}" \
    "#bar div{height:100%%;background:#4caf50;width:0%%;transition:width 0.2s}" \
    "</style></head><body>" \
    "<h2>SX70z OTA Upgrade</h2>" \
    "<table>" \
    "<tr><td>Serial</td><td>%s</td></tr>" \
    "<tr><td>Model</td><td>%s</td></tr>" \
    "<tr><td>Firmware</td><td>v%u.%u.%u</td></tr>" \
    "<tr><td>Built</td><td>%s</td></tr>" \
    "<tr><td>Hardware</td><td>Rev %u</td></tr>" \
    "</table>" \
    "<p><a href='/calibration'>Shutter Calibration</a></p>"

#define HTML_TAIL \
    "<p>Select firmware .bin file to upgrade.</p>" \
    "<input type='file' id='file' accept='.bin'><br>" \
    "<button onclick='upgrade()'>Start Upgrade</button>" \
    "<div id='bar'><div id='prog'></div></div>" \
    "<p id='msg'></p>" \
    "<script>" \
    "function upgrade(){" \
    "  var f=document.getElementById('file').files[0];" \
    "  if(!f){alert('Select a .bin file first');return;}" \
    "  var x=new XMLHttpRequest();" \
    "  x.upload.onprogress=function(e){" \
    "    if(e.lengthComputable){" \
    "      var p=Math.round(e.loaded/e.total*100);" \
    "      document.getElementById('prog').style.width=p+'%%';" \
    "      document.getElementById('msg').textContent='Uploading... '+p+'%%';" \
    "    }" \
    "  };" \
    "  x.onload=function(){" \
    "    if(x.status==200){" \
    "      document.getElementById('msg').textContent='Done! Restarting...';" \
    "    }else{" \
    "      document.getElementById('msg').textContent='Failed: '+x.responseText;" \
    "    }" \
    "  };" \
    "  x.onerror=function(){" \
    "    document.getElementById('msg').textContent='Network error';" \
    "  };" \
    "  x.open('POST','/update');" \
    "  x.setRequestHeader('Content-Type','application/octet-stream');" \
    "  x.send(f);" \
    "}" \
    "</script></body></html>"

/* ================================================================
 *  GET / → 返回上传页面（含设备信息）
 * ================================================================ */

static esp_err_t get_handler(httpd_req_t *req)
{
    char page[2048];
    int len = snprintf(page, sizeof(page),
                       HTML_HEAD HTML_TAIL,
                       device.serial,
                       device.model,
                       device.sw_major, device.sw_minor, device.sw_patch,
                       device.build_time,
                       device.hw_rev);

    if (len >= sizeof(page)) {
        ESP_LOGW(TAG, "HTML page truncated (%d > %zu)", len, sizeof(page));
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, page, len);
    return ESP_OK;
}

/* ================================================================
 *  POST /update → 接收原始固件二进制，写入 OTA 分区，重启
 * ================================================================ */

static esp_err_t post_update_handler(httpd_req_t *req)
{
    const esp_partition_t *part = esp_ota_get_next_update_partition(NULL);
    if (!part) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "No OTA partition");
        return ESP_FAIL;
    }

    // content_len 可能为 -1（chunked 编码），记录日志但不用来限制循环
    // 挂起 Core 1 控制任务，Flash 写期间不能有任何代码访问 Flash
    camera_pause();

    ESP_LOGI(TAG, "OTA start — partition: %s, content_len: %d",
            part->label, req->content_len);

    esp_ota_handle_t ota;
    esp_err_t err = esp_ota_begin(part, OTA_SIZE_UNKNOWN, &ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin: %d", err);
        camera_resume();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA init failed");
        return ESP_FAIL;
    }

    // 用静态内存避免溢出 HTTPD 默认 4KB 栈
    static char buf[4096];
    int total = 0;
    while (1) {
        int ret = httpd_req_recv(req, buf, sizeof(buf));
        if (ret == 0) break;                       // 传输完成
        if (ret < 0) {
            ESP_LOGE(TAG, "Receive error at %d, ret=%d", total, ret);
            esp_ota_abort(ota);
            camera_resume();
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Transfer error");
            return ESP_FAIL;
        }
        err = esp_ota_write(ota, buf, ret);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write error at %d: %d", total, err);
            esp_ota_abort(ota);
            camera_resume();
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                                "Flash write error");
            return ESP_FAIL;
        }
        total += ret;
    }

    err = esp_ota_end(ota);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end: %d", err);
        camera_resume();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "OTA finalize failed");
        return ESP_FAIL;
    }

    err = esp_ota_set_boot_partition(part);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Set boot partition: %d", err);
        camera_resume();
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR,
                            "Boot partition error");
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Boot partition set to: %s", part->label);

    // 验证启动分区确实设对了
    const esp_partition_t *boot_part = esp_ota_get_boot_partition();
    if (boot_part) {
        ESP_LOGI(TAG, "Verified boot partition: %s (0x%" PRIx32 ")",
                 boot_part->label, boot_part->address);
    }

    ESP_LOGI(TAG, "OTA done — %d bytes written to %s, restarting...",
             total, part->label);
    httpd_resp_sendstr(req, "OK — restarting");
    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

/* ================================================================
 *  GET /calibration → 快门校准值编辑页面
 * ================================================================ */

#define CAL_HTML_HEAD \
    "<!DOCTYPE html><html><head><meta charset='utf-8'>" \
    "<meta name='viewport' content='width=device-width,initial-scale=1'>" \
    "<title>SX70z Calibration</title>" \
    "<style>" \
    "body{font-family:sans-serif;max-width:420px;margin:40px auto;padding:0 20px}" \
    "table{width:100%%;border-collapse:collapse;margin:12px 0}" \
    "td{padding:3px 6px;border:1px solid #ddd}" \
    "td:first-child{background:#f5f5f5;font-weight:bold}" \
    "input[type=number]{width:80px;font-size:14px;padding:2px 4px}" \
    "h2{color:#333}.ok{color:green}.warn{color:#e65100}" \
    "button{font-size:15px;padding:8px 16px;margin:8px 4px 8px 0}" \
    "</style></head><body>" \
    "<h2>Shutter Calibration</h2>" \
    "<p><a href='/'>← Back to OTA</a></p>" \
    "<p class='%s'>%s</p>" \
    "<form method='POST' action='/calibration'>" \
    "<table><tr><td>Speed</td><td>x10 (0.1ms)</td></tr>"

#define CAL_HTML_TAIL \
    "</table>" \
    "<button type='submit'>Save</button>" \
    "<button type='button' onclick=\"if(confirm('Reset to factory defaults?'))" \
    "{location.href='/calibration?reset=1'}\">Reset to Defaults</button>" \
    "</form></body></html>"

static esp_err_t get_calibration_handler(httpd_req_t *req)
{
    /* 检查 ?reset=1 查询参数 */
    char query[32] = {0};
    httpd_req_get_url_query_str(req, query, sizeof(query));
    if (strstr(query, "reset=1")) {
        shutter_cal_reset_defaults();
        /* 删除 NVS 中的校准数据（不写入默认值），使重启后仍识别为"未校准" */
        shutter_cal_erase();
        httpd_resp_set_status(req, "303 See Other");
        httpd_resp_set_hdr(req, "Location", "/calibration");
        httpd_resp_send(req, NULL, 0);
        return ESP_OK;
    }

    static char page[6144];
    const char *css_class = shutter_cal_is_valid() ? "ok" : "warn";
    const char *status_text = shutter_cal_is_valid()
        ? "&#10004; Calibrated (custom values from NVS)"
        : "&#9888; Using factory defaults (not yet calibrated)";

    int off = snprintf(page, sizeof(page), CAL_HTML_HEAD, css_class, status_text);
    if (off < 0 || off >= sizeof(page)) goto toolong;

    const uint16_t *vals = shutter_cal_get_array();   /* 加锁 */
    for (int i = 0; i < SHUTTER_SPEED_COUNT; i++) {
        int n = snprintf(page + off, sizeof(page) - off,
                         "<tr><td>%s</td>"
                         "<td><input type='number' name='s%d' value='%u' min='1' max='65535'></td></tr>",
                         get_shutter_speed(i), i, vals[i]);
        if (n < 0 || off + n >= sizeof(page)) {
            shutter_cal_unlock();                      /* 解锁后跳出 */
            goto toolong;
        }
        off += n;
    }
    shutter_cal_unlock();                              /* 正常解锁 */

    {
        int n = snprintf(page + off, sizeof(page) - off, "%s", CAL_HTML_TAIL);
        if (n < 0 || off + n >= sizeof(page)) goto toolong;
        off += n;
    }

    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, page, off);
    return ESP_OK;

toolong:
    ESP_LOGW(TAG, "Calibration page truncated");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Page too large");
    return ESP_FAIL;
}

/* ================================================================
 *  POST /calibration → 解析表单并保存校准值到 NVS
 * ================================================================ */

static esp_err_t post_calibration_handler(httpd_req_t *req)
{
    /* 读取表单体 */
    int total = req->content_len;
    if (total <= 0 || total > 1024) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid content length");
        return ESP_FAIL;
    }
    char body[1024];
    int received = 0;
    while (received < total) {
        int ret = httpd_req_recv(req, body + received, total - received);
        if (ret <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Read error");
            return ESP_FAIL;
        }
        received += ret;
    }
    body[received] = '\0';

    /* 解析 s0=xxx&s1=xxx&... */
    uint16_t *cal = shutter_cal_get_array();
    char *saveptr = NULL;
    char *token = strtok_r(body, "&", &saveptr);
    while (token) {
        char *eq = strchr(token, '=');
        if (eq) {
            *eq = '\0';
            const char *key = token;
            const char *val = eq + 1;
            if (key[0] == 's' && key[1] >= '0' && key[1] <= '9') {
                int idx = atoi(key + 1);
                if (idx >= 0 && idx < SHUTTER_SPEED_COUNT) {
                    int v = atoi(val);
                    if (v >= 1 && v <= 65535) {
                        cal[idx] = (uint16_t)v;
                    }
                }
            }
        }
        token = strtok_r(NULL, "&", &saveptr);
    }
    shutter_cal_unlock();

    shutter_cal_save();
    shutter_cal_set_valid(true);
    ESP_LOGI(TAG, "Calibration updated via web");

    /* 303 重定向回校准页面 */
    httpd_resp_set_status(req, "303 See Other");
    httpd_resp_set_hdr(req, "Location", "/calibration");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

static httpd_handle_t g_server = NULL;

/* ================================================================
 *  公开接口
 * ================================================================ */

esp_err_t ota_web_start(void)
{
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 8;
    config.recv_wait_timeout = 30;   // 上传大固件不能超时太短
    config.send_wait_timeout = 10;

    esp_err_t err = httpd_start(&g_server, &config);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP server start failed: %d", err);
        return err;
    }

    httpd_register_uri_handler(g_server, &(httpd_uri_t){
        .uri = "/", .method = HTTP_GET, .handler = get_handler
    });
    httpd_register_uri_handler(g_server, &(httpd_uri_t){
        .uri = "/update", .method = HTTP_POST, .handler = post_update_handler
    });
    httpd_register_uri_handler(g_server, &(httpd_uri_t){
        .uri = "/calibration", .method = HTTP_GET, .handler = get_calibration_handler
    });
    httpd_register_uri_handler(g_server, &(httpd_uri_t){
        .uri = "/calibration", .method = HTTP_POST, .handler = post_calibration_handler
    });

    ESP_LOGI(TAG, "Ready — open http://<IP>/ in browser");
    return ESP_OK;
}
