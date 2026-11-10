# AGENTS.md

This file provides guidance to coding agents working with this repository.

## Project Overview

Polaroid SX-70 相机控制器，基于 ESP32-PICO-D4 (4MB Flash, 无 PSRAM)，运行 ESP-IDF v5.5.2。

- **硬件**: 自绘 PCB，ESP32-PICO-D4、闪光灯、快门/光圈控制、机身板贴装 PCF8574TS/3 I2C GPIO 扩展、SSD1306 OLED、OPT4001 环境光传感器
- **机身板地址**: 原始原理图和 BOM 标注 PCF8574ATS，实际贴装 PCF8574TS/3，投产目录 BOM 已更正。网表中 A0/A1/A2 接地，实际 PCF8574 地址为 0x20，与源码一致。源码用 PCF8575 驱动连续读两字节，按键位 8/9/10 对应第二次读取的 P0/P1/P2；作者确认现有固件适配实际芯片。下文 PCF8575 的描述为代码行为。
- **功能**: 自动测光曝光、B/T/手动模式、3D 按键菜单、BLE + WiFi 配网、HTTP OTA 升级
- **仓库**: `SX70z` — 独立固件仓库，硬件设计在 `../Hardware/` 下
- **ESP-IDF**: `D:\ESPIDF\v5.5.2\esp-idf` / **工具链**: `D:\ESPIDF_TOOL`

## 代码审查

- **2026-11-10 基线 `ca8716b`**：完整静态审查见 [`docs/review/2026-11-10-code-review.md`](docs/review/2026-11-10-code-review.md)。
- 修改 OTA、快门/执行器时序、显示与 I2C、Web handler 前，先读该记录的对应条目；高优先级项目前尚未修复。
- 当前最需要关注：OTA 只暂停 `control_task`（R01）、S3/S5/S1T 等待无超时（R02）、`vTaskSuspend` 可能持有 I2C 锁（R03）、Web 缓冲区边界（R04/R05）、校准保存/重置失败仍显示成功（R06）、B/T 闪光时 SOL2 长期 100% 通电（R07）。

## Build & Flash

```bash
idf.py build                      # 编译（sdkconfig.defaults 自动合并到 sdkconfig）
idf.py -p <PORT> flash            # 烧录 (UART, QIO 80MHz)
idf.py -p <PORT> monitor          # 串口监视 (UART0, 115200 baud)
idf.py -p <PORT> erase-flash flash monitor
```

非默认配置项应记录在 `sdkconfig.defaults`（纯 ASCII，无中文注释），`sdkconfig` 在 `.gitignore` 中。当前本地 `sdkconfig` 里还有 CPU 240MHz、CPU1 idle WDT 关闭、`CONFIG_COMPILER_OPTIMIZATION_SIZE` 等关键项未写入 defaults，干净构建会与发布固件不同（审查 R29）。

## Architecture

### 核心设计原则

- **双核隔离**: Core 0 跑所有非控制逻辑（WiFi/BLE/HTTP/日志），Core 1 只跑时序敏感的相机控制。**与控制无关的代码一律放 Core 0**
- **非阻塞初始化**: WiFi 配网、BLE 连接在后台任务运行，不阻塞主控
- **事件驱动**: WiFi/IP 状态变更通过 `esp_event` 回调通知
- **OTA 安全**: OTA 写 Flash 前调用 `camera_pause()`，完成后恢复或重启。当前实现只挂起 `control_task`，`display_task` / `metering_task` / `shutter_task` 仍可能运行；这是审查 R01 的待修复项。修改 OTA 时必须确保所有 Core 1 任务停止并等待快门空闲。

### CPU 分配

```
Core 0:  WiFi 协议栈 + NimBLE BLE + HTTP Server (OTA Web) + 事件回调 + app_main
Core 1:  相机控制（与射频中断隔离），4 个任务按优先级分层：
         Shutter Task  prio 10  强时序引脚控制（平时阻塞，触发后独占 Core 1）
         Control Task  prio 8   主控逻辑（按键/闪光/S1，30ms 周期）
         Display Task  prio 5   SSD1306 I2C 刷屏（200ms 周期，与按键解耦）
         Metering Task prio 3   后台测光（OPT4001，1s 周期）
```

**优先级定义**见 `camera_main.h`：`SHUTTER_TASK_PRIO=10`, `CONTROL_TASK_PRIO=8`, `DISPLAY_TASK_PRIO=5`, `METERING_TASK_PRIO=3`

**Shutter Task 独占机制**：优先级最高 + 忙等不释放 CPU，保证时序不受其他任务抢占。通过 `xTaskNotifyGive` 触发，执行完回到 `ulTaskNotifyTake` 阻塞。未来快门/电机等几十秒的强时序工作均在此任务完成。**注意**：忙等只用于微秒级曝光时序；S3/S5/S1T 等可能长时间等待的路径应加超时并改用 `vTaskDelay()`，否则会饿死 Core 1 其他任务（审查 R02）。

### 目录结构

```
SX70_ModelZ/
├── main/
│   ├── main.c              # WiFi/BLE 初始化，启动控制任务，触发 OTA Web
│   └── CMakeLists.txt      # REQUIRES: nvs_flash esp_wifi esp_event esp_netif wifi_provisioning src
├── src/
│   ├── CMakeLists.txt      # REQUIRES: esp_http_server app_update driver esp_timer esp_driver_gptimer esp_lcd nvs_flash
│   ├── devinfo.h / .c      # 设备信息（序列号=芯片 MAC、软硬件版本）
│   ├── camera_main.h / .c  # 相机控制任务 (Core 1)，camera_state_t 类型定义，camera_pause/resume
│   ├── shutter_cal.h / .c  # 快门校准值 NVS 持久化 + 互斥锁保护，Web 端可编辑
│   ├── display_manager.h / .c # SSD1306 显示帧绘制（show_frame / show_taking / show_menu / countdown）
│   ├── gpio_inputs.h / .c  # GPIO 输入处理：S1/S2 去抖、3D 按键、菜单/模式切换
│   ├── opt4001.h / .c      # OPT4001 环境光传感器驱动（I2C0, 0x44）
│   ├── ssd1306.h / .c      # SSD1306 OLED 显示驱动（I2C1）
│   ├── pcf8575.h / .c      # PCF8575 I2C GPIO 扩展（I2C1, 0x20-0x27）
│   ├── font.h              # 字体类型定义
│   ├── fonts/font5x8.h     # 5x8 像素字体（默认）
│   ├── fonts/font5x8.c     # 5x8 字体数据（列式存储，每字符 5 字节）
│   └── ota_web.h / .c      # HTTP 网页上传固件 OTA
├── components/             # ESP-IDF 标准组件目录（当前为空）
├── docs/                   # 设计文档；review/ 下为代码审查记录
├── tests/ui_host/          # 主机端 UI 绘制测试（不模拟 ESP LCD）
├── tools/                  # 字库/图标转换脚本
├── sdkconfig               # 本地生成，已 gitignore
├── sdkconfig.defaults      # 非默认配置（kconfgen 合并）
└── .clangd
```

### 初始化流程

```
app_main() [Core 0]
  0. devinfo_init()                        — 读芯片 MAC 做序列号
  1. OTA 回滚检查                           — PENDING_VERIFY/NEW 时调用
                                             esp_ota_mark_app_valid_cancel_rollback()
                                             （当前在初始化前确认，见审查 R18）
  2. pin_init()                            — GPIO + I2C0/I2C1 初始化
  3. NVS 初始化                             — WiFi 凭据、BLE 绑定、校准值
  3.5 shutter_cal_init()                   — 从 NVS 加载快门校准值
  3.6 flight_cfg_init()                    — 加载飞行模式
  4. xTaskCreatePinnedToCore(control_task, 1) — 启动 Core 1 控制任务（prio 8）
     └─ control_task() 内部：
        ├─ SSD1306 初始化（esp_lcd 官方驱动, 128×32） + PCF8575 初始化
        ├─ 创建 display_task (prio 5) — OLED 200ms 周期 I2C 刷屏
        ├─ 创建 metering_task (prio 3) — OPT4001 初始化 + 1s 周期测光
        ├─ 初始化 GPTimer — us 级精确定时 (1MHz, intr_priority=1 绑定 Core 1)
        ├─ 初始化 LEDC — SOL1/SOL2 PWM
        ├─ 创建 shutter_task (prio 10) — 平时阻塞，xTaskNotifyGive 触发
        └─ 主循环：按键 + S2 + S1T (30ms 周期；读 PCF8575 会占用 I2C1，可能被 display 刷屏阻塞)
  5. 飞行模式检查：ON 则跳过网络栈，Core 0 进入空闲循环
  6. esp_netif + event loop                — 网络栈基础
  7. 注册 WiFi / IP / Provisioning 事件回调
  8. WiFi STA 启动 + 设置主机名 "SX70z"
  9. BLE Provisioning（非阻塞）             — 未配网则广播，已配网则直接连 WiFi
  10. Core 0 空闲循环（打印 RSSI 等）
```

### OTA 升级流程

```
IP_EVENT_STA_GOT_IP → ota_web_start()
  → 浏览器打开 http://<ESP32_IP>
  → 首页：设备信息 + OTA 固件上传 + "Shutter Calibration" 链接
  → GET /calibration：27 档快门校准值编辑表格 + 校准状态指示
  → POST /calibration：解析表单 → 更新内存数组 → 写入 NVS → 303 重定向
  → GET /calibration?reset=1：恢复出厂默认值 + 删除 NVS key → 重启后仍为"未校准"
  → 选 .bin 文件上传
  → POST /update:
       camera_pause()              ← 当前仅挂起 control_task（R01：需扩展到全部 Core 1 任务）
       esp_ota_begin()             ← 打开 ota_1 分区
       esp_ota_write() × N         ← 逐块写入 Flash
       esp_ota_end()               ← 校验
       esp_ota_set_boot_partition(ota_1)
       esp_ota_get_boot_partition() ← 打印启动分区（尚未校验，R20）
       esp_restart()               ← 重启进入新固件
    错误路径: camera_resume()      ← 恢复 control_task

  重启后 → app_main 步骤 1 检测到 PENDING_VERIFY → 调用
  esp_ota_mark_app_valid_cancel_rollback() 确认新固件有效，回滚取消
```

### OTA Web 端点

| 方法 | 路径 | 功能 |
|------|------|------|
| GET | `/` | 首页：设备信息 + OTA 上传 + 校准链接 |
| POST | `/update` | 固件 OTA 升级（camera_pause → 流式写 Flash → 重启） |
| GET | `/calibration` | 快门校准编辑页面（27 行可编辑表格 + 状态指示） |
| POST | `/calibration` | 保存校准值到 NVS（application/x-www-form-urlencoded） |
| GET | `/calibration?reset=1` | 恢复出厂默认值 + 删除 NVS key |

> 当前所有 Web 端点均无认证，BLE PoP 也是硬编码；同一局域网内可刷固件或改校准（审查 R33）。若只在可信网络使用，也要在文档中明确这一限制。

### 快门校准模块（shutter_cal.h / .c）

**NVS 结构**：namespace `"cal"`，key `"shutter"`，blob = `{ uint32_t magic; uint16_t values[27]; }`（有效载荷 58 字节，`sizeof` 为 60，含尾部对齐）

**magic 校验**：`0x53583000 | SHUTTER_SPEED_COUNT`，固件改变快门档位数则自动失效，回退到出厂默认值

**`cal_is_valid` 标志**：
- `false`：NVS 无数据 / magic 不匹配 / 用户点了 Reset → 页面显示 ⚠️ "Using factory defaults"
- `true`：用户通过 Web 保存过 → 页面显示 ✅ "Calibrated"
- Reset 操作删除 NVS key（不写入默认值），确保重启后仍为 `false`

**互斥锁**：`shutter_cal_get_array()` 获取锁 → 修改数据 → `shutter_cal_unlock()` 释放。`get_shutter_time_x10()` 内部自动加解锁。`shutter_task` (Core 1) 读取与 Web handler (Core 0) 写入通过锁序列化。

> 这套 API 返回裸指针并由调用者负责解锁，容易遗漏；后续建议增加 `shutter_cal_update()` / `shutter_cal_snapshot()`，避免指针逃逸（后续接口改进）。另外，`shutter_cal_save()` 的返回值必须在 Web handler 中检查，失败时不能标记为已校准（R06）。

### OTA 回滚机制

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 启用后：

1. `esp_ota_set_boot_partition()` 将新分区标记为 `PENDING_VERIFY`
2. 新固件启动后应调用 `esp_ota_mark_app_valid_cancel_rollback()` 确认；若在确认前崩溃或重启，Bootloader 会将当前分区标记为 `INVALID`，下次启动自动回滚到上一个有效分区
3. 这不是“bootloader 定时器”机制：确认可以延后到控制任务、显示、WiFi 初始化成功之后。当前代码在 `app_main` 最开头就确认（审查 R18），会降低“能启动但功能损坏”固件的自动回滚能力
4. 若新固件启动期间崩溃（LoadProhibited 等），Bootloader 同样自动回滚

关键调用位置在 `app_main` 步骤 1；修改时应按 R18 重新评估确认时机。

### BLE / WiFi 技术栈

| 组件 | 选型 |
|---|---|
| BLE 协议栈 | NimBLE (VHCI) |
| WiFi 配网 | BLE Provisioning (Proof of Possession) |
| OTA | WiFi HTTP 网页上传 |
| 分区表 | IDF 内置 `CONFIG_PARTITION_TABLE_TWO_OTA_LARGE` (ota_0 + ota_1 各 1700KB) |

### sdkconfig 关键配置

- `CONFIG_IDF_TARGET="esp32"` — PICO-D4 在 IDF 中归类为 ESP32
- `CONFIG_BT_NIMBLE_ENABLED=y` — NimBLE 协议栈
- `CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y` — 双 OTA (各 1700KB)
- `CONFIG_WIFI_PROV_BLE_SEC_CONN=y` — BLE 安全配网
- `CONFIG_WIFI_PROV_KEEP_BLE_ON_AFTER_PROV=y` — 允许调用 `wifi_prov_mgr_keep_ble_on()`；当前代码未调用，配网结束后 BLE 停止
- `CONFIG_BT_NIMBLE_NVS_PERSIST=y` — BLE 配对持久化
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` — OTA 失败回滚
- 优化级别：当前本地 `sdkconfig` 使用 `CONFIG_COMPILER_OPTIMIZATION_SIZE=y` + assertions；`sdkconfig.defaults` 只显式取消 DEBUG，干净构建可能回到 IDF 默认。发布前必须确认（R29）
- `CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y` / `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1` 关闭目前只在本地 `sdkconfig`，未写入 defaults；长曝光/自拍忙等依赖这两项（R29）
- `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` / `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y` — QIO 80MHz
- `CONFIG_LOG_DEFAULT_LEVEL_WARN=y` — 发布版日志级别 WARN（sdkconfig.defaults），开发时可覆盖为 DEBUG
- BLE 设备名 `"SX70z"` / 广播名 `"SX70z_POP"` / PoP `"sx70z123"`
- WiFi 主机名 `"SX70z"`

### 日志级别约定

| 级别 | 用途 | 发布版（WARN） |
|---|---|---|
| `ESP_LOGE` | 错误（始终打印） | ✅ |
| `ESP_LOGW` | 警告 | ✅ |
| `ESP_LOGI` | 重要信息：WiFi 连接/断开、配网成功/失败、OTA 进度、设备信息 | ❌ 编译时裁掉 |
| `ESP_LOGD` | 调试细节：STA 启动、配网开始/结束、凭据收到、WiFi 未连接 | ❌ 编译时裁掉 |
| `ESP_LOGV` | 未启用 | ❌ |

`sdkconfig.defaults` 默认 WARN 级别（省 Flash）。开发时在本地 `sdkconfig` 中覆盖为 DEBUG 即可恢复 I/D 级别。

## Peripheral Hardware & I2C Bus

| 总线 | 引脚 | 速度 | 设备 |
|------|------|------|------|
| I2C0 | GPIO21/22 | 100kHz | OPT4001 (0x44) |
| I2C1 | GPIO7/8 | 100kHz | SSD1306 (0x3C), PCF8575 (0x20-0x27) |

> I2C1 代码当前为 100kHz（`pin_init.c`）。400kHz 只是待验证的优化项（R09），不是现状。

### OPT4001 环境光传感器

- 挂 I2C_NUM_0（GPIO21/22），地址 0x44
- 初始化在 `metering_task` (Core 1, prio 3) 中调 `opt4001_init()`，由 `control_task` 创建该任务
- 自动量程模式，800ms 转换周期，连续采样 — 初始化后首次有效数据延时 900ms
- I2C 读时序：每次 lux 需读 MSB/LSB 两个寄存器，共 4 次独立事务（写地址 → STOP → 读数据 → STOP）；两次读之间可能跨转换边界，建议 burst 读或检查 conversion-ready（审查 R12）
- 量程 0.001 ~ 2,200,000 lux，12 档硬件自动切换

### SSD1306 OLED

- 挂 I2C_NUM_1（GPIO7/8），默认地址 0x3C，100kHz
- **128×32 像素**（注意不是 64！）
- 基于 ESP-IDF 官方 `esp_lcd` 驱动（`esp_lcd_panel_ssd1306`），init/写屏/开关由官方库处理
- 自维护本地帧缓冲 `dev->buff`（512 bytes），绘图 API 保留：`ssd1306_draw_str/line/rect/circle` 等
- `ssd1306_show()` 通过 `esp_lcd_panel_draw_bitmap()` 全量刷新到屏幕；每 200ms 刷 512 字节，100kHz 下约 46ms，当前没有 dirty 检查（R10）
- 字体使用 `font5x8_font`（5×8 像素，96 字符 ASCII）
- `ssd1306_draw_str_black()` — 白底上画黑字，用于状态栏黑字标记（自拍/多重曝光）
- 8×16 字体（`font8x16_font`）从 Spleen BDF 转换，当前竖屏页面未使用（保留）
- 6×12 字体（`font6x12_font`）从 Spleen BDF 转换，状态栏/标题用
- 8×14 Terminus Bold（`font_terminus8x14_font`）用于当前竖屏主页的大数字/快门速度
- **绘图 API**（ssd1306 驱动层）：`ssd1306_draw_str`、`ssd1306_draw_str_black`、`ssd1306_draw_line/rect/circle/pixel`

### PCF8575 I2C GPIO 扩展

- 挂 I2C_NUM_1（GPIO7/8），地址 0x20-0x27
- 16 位 GPIO，方向可逐位配置（1=输入 0=输出）
- API：`pcf8575_init/read/write/write_pin/read_pin/set_input/set_output`

### 快门控制 (Shutter Expose)

快门曝光时序，运行在 `shutter_task` (prio 10)：

```
shutter_task 主循环：
  等待 xTaskNotifyTake → 检查 menu < 10
  1. 关快门 (SOL1 high, 30ms)
  2. 电机启动 → 等 S3 反光板就位 → 电机停
  3-6. single_shut() 单次曝光（详见下文）
     多重曝光循环：while (multi_exp_remain > 0) 重复调用
  7. 电机吐片 → 等 S5 胶片检测
  8. 等 S1T 释放防连拍
```

**`single_shut()` 单次曝光**（步骤 3~6，提取为独立函数供多重曝光复用）：

```
  3. Y Delay：自拍倒计时（suspend 其他任务 + countdown）→ 闪光模式 SOL2 光圈 → 18ms
  4. 曝光 (根据 mode):
     '1' + 无闪光 — 开快门 → delay_us(时间×100) → 关快门
     '1' + 有闪光 — 开快门 → 47ms → FF 脉冲 → gap 延时 → 关快门
     'B' Bulb    — 开快门 → 15ms → [闪光] → 等 S1T 释放
     'T' Time    — 开快门 → [闪光] → 等 S1T 释放 → 等 S1T 再按下
  5. 关快门 + 30ms + 18ms
  6. 光圈归位 (闪光模式)
```

> 闪光分支使用 `gap = max(shutter_delay_x10 - 470, 0)`，因此比 1/20 更快的档位在闪光路径下实际会变成约 48ms；这是现有设计（见 `Camera.md`），修改前先确认。`single_shut` 内的所有等待目前没有超时，见审查 R02。

**多重曝光**：
- `multi_exp_remain = 0` → 正常 1 次曝光
- `multi_exp_remain = N` → 1 + N 次曝光（每次 S1T 触发）
- `multi_exp_remain = -1` → 仅中止（首张防护，仍拍 1 张）；中止通常还需要一次 S1T 触发，见审查 R21
- 每次额外曝光前重新加载 shutter_speed / mode / use_flash，允许张间修改

- 快门速度表：27 档 (`"3s"` ~ `"1/8000"`)，校准值存储在 NVS（`shutter_cal.c`），Web 端可编辑无需重编译
- 快门校准模块 `shutter_cal`：`shutter_cal_x10[]` 运行时可写（单位 0.1ms，是**控制延时，不是曝光时间**，见 `复刻指南.md`），`shutter_cal_init()` 从 NVS 加载（magic=`0x53583000|27` 校验），失败则使用 `shutter_cal_default[]` 出厂值；`shutter_cal_get_array()` 获取锁后返回指针，用完需调 `shutter_cal_unlock()`
- SOL1/SOL2 通过 LEDC PWM 控制（31.25kHz, 8-bit），100% 吸合 / 40% 保持
- **注意**：当前 `single_shut()` 在 B/T 门 + 闪光时会让 SOL2 保持 100% 直到曝光结束；长时间 B/T 有发热/耗电风险（审查 R07），后续应增加 hold duty 或限时
- `#define HAS_FOCUS 0` 控制对焦功能编译开关，当前为 TODO 占位
- S1T 去抖：`gpio_inputs.c` 中 `debounce_read_s1pin()` 为计数器方式（`S1_DEBOUNCE_COUNT=5`）。当前返回值语义与命名相反：松开时返回 true，按住约 150ms 后返回 false；`control_task` 用 `!返回值` 触发快门（审查 R08）。修改按键逻辑时不要按“返回 true 表示按下”理解

## Development Notes

### camera_state_t 参数结构体

相机所有状态集中在 `camera_state_t camera_state`（`camera_main.c` 定义，`camera_main.h` extern 声明），含嵌套子结构：

```
camera_state_t
├── if_display                  OLED 是否可用（可用时 LED 保持熄灭）
├── has_flash                   闪光灯是否接入（gpio_inputs_read_s2 防抖检测）
├── wifi_enabled                WiFi STA 服务已启动（不等于已连接，R15）
├── ble_enabled                 BLE 配网服务已启动（不等于已连接）
├── metering_state_t metering   测光参数
│   ├── last_lux                校准后 lux（= raw × METERING_ATTEN_K）
│   ├── last_lux_raw            OPT4001 原始读数
│   ├── ev                      校准后 EV（ISO 640）
│   ├── ev_raw                  原始 EV
│   └── auto_shutter_pos        AUTO 模式计算的快门速度索引
├── button_state_t button       按键参数
│   ├── available               PCF8575 是否可用
│   ├── old_value[4]            上次按键状态（"111" 格式）
│   ├── debounce_last           防抖计时 (ms)
│   └── push_down_start         按下计时 (ms)
├── menu / time_mode / cam_mode[10] / shut_mode / shutter_speed  曝光/模式参数（shut_mode: '1'=Normal, 'B'=Bulb, 'T'=Time）
├── self_timer_sec               自拍定时设置值（0/2/5/10s）
├── multi_exp_remain             额外多重曝光张数（0=正常，N=追加N张，-1=仅中止）
├── ip_str[16]                   WiFi IP 地址（空字符串=未连接）
├── flight_mode                  飞行模式：0=关闭，1=开启（NVS 持久化，重启生效）
└── test_led_level              调试 LED 电平（仅 OLED 不可用时闪烁）
```

> 该结构体目前声明为 `volatile`，但这只影响编译器优化，不提供跨任务同步；多字段一致性仍需临界区或快照（审查 R17）。`menu_item_idx` / `menu_adjust` 是 `gpio_inputs.c` 中的独立全局变量，同样被 control/display 两个任务共享。

### 3D 按键处理（gpio_inputs.c，PCF8575）

通过 PCF8575 I2C GPIO 扩展读取 3 个物理按键（上/下/按下）。

**按键引脚**（`PIN.h`）：`PCF_BUTTON3D_DOWN=10`, `PCF_BUTTON3D_UP=8`, `PCF_BUTTON3D_PUSH=9`

**核心函数**：
- `read_3d_button_pins(char *result)` — 读 PCF8575 16 位状态，提取按键位到 `"101"` 字符串
- `button3d_handler()` — 100ms 采样防抖 + 下降沿/上升沿检测 + 长短按区分（>1000ms）
- `down_button_call()` / `up_button_call()` — M 档调整快门速度；B/T 调整 `time_mode`；menu=10 时切换参数或调整当前值
- `push_button_short()` — 拍摄模式循环 AUTO→BULB/TIME→MANUAL；菜单内切换调整模式
- `push_button_long()` — 进入/退出设置菜单（menu=10）。退出回到 AUTO
- `update_mode_display()` — 根据 `menu` 更新 `cam_mode` + `shut_mode` 字符串
- `gpio_inputs_read_s2()` — S2 闪光灯检测（按调用次数饱和防抖），返回 `bool`（接入=true）
- `debounce_read_s1pin()` — S1T 计数器防抖。**注意返回值语义与命名相反**：松开时 true，按住约 150ms 后 false；`control_task` 用 `!返回值` 触发快门（审查 R08）

**菜单结构**：
| menu | 模式 | cam_mode | 快门来源 |
|------|------|----------|---------|
| 0 | AUTO | "AUTO" | auto_shutter_pos (测光自动) |
| 1 | BULB/TIME | time_mode 值 | B/T/秒值（上下键设置） |
| 2 | MANUAL | 快门速度字符串 | 手动选择（上/下键） |
| 10 | 设置菜单 | "Menu 0" | 参数列表（上下键切换，短按选中调值） |

**主循环调用**：`control_task` 每 30ms 调 `button3d_handler()`，内部再做 100ms 采样防抖，位于闪光灯检测之前。由于 display 任务会占用 I2C1，实际循环周期可能抖动到 30–80ms，S1/S2 的“按次数”去抖时间并不固定（审查 R08）。

### us 级精确定时 (`delay_us`)

基于 GPTimer 实现（`camera_main.c:23-49`）：
- `g_delay_timer` — GPTimer handle (1MHz = 1μs, one-shot alarm, 无自动重载)
- `delay_timer_cb` — `IRAM_ATTR` ISR 回调设 `g_timer_done` 标志位
- **`intr_priority = 1`** 确保 ISR 绑定到 Core 1（`priority=0` 走默认分配，不保证核亲和）
- `delay_us(uint32_t us)` — 重设 alarm + 启动 timer + 忙等，期间不释放 CPU
- **`static` 已移除**：`delay_us` 在 `camera_main.h` 中声明为 extern，供 `gpio_inputs.c` 等模块调用
- 精度：±2μs（硬件定时器 ISR 同核直连，零跨核开销）
- IRAM 安全：驱动内部 `GPTIMER_INTR_ALLOC_FLAGS` 默认含 `ESP_INTR_FLAG_IRAM`

### Shutter Task 使用方式

```c
// 触发快门动作（可在 control_task 或其他任务中调用）
xTaskNotifyGive(shutter_task_handle);
// shutter_task 立即抢占 Core 1，执行完回到阻塞态
```

### 自拍定时

**设置**（`menu=10`）：
- 上/下键在 `{0, 2, 5, 10}` 四档间循环，值存储在 `camera_state.self_timer_sec`
- OLED：设置菜单中显示 `SelfTimer` 当前值；主页状态行（y=110）显示 `T<定时>M<多曝>`，定时为 0 时显示 `T--`

**倒计时流程**（`shutter_task` 曝光前）：
1. `self_timer_sec > 0` 时，先 suspend control/display/metering 三个任务
2. 每秒调 `display_show_countdown()` 在 32×128 竖屏居中显示剩余秒数
3. `delay_us(1000000)` 忙等 1s
4. 倒计时结束后 resume 三个任务，进入正常曝光流程

> 当前“刷屏一次再忙等 1s”的方式会把刷屏 I2C 时间累加到每一秒，10 秒倒计时可能慢 0.4–0.5 秒（审查 R22）。另外 suspend 其他任务与 I2C 锁存在冲突风险（R03）。

**辅助函数**：
- `display_show_countdown(disp, int32_t seconds_remaining)` — 清屏后居中显示秒数（int32 覆盖 T 门最大 18000s=5h），负数直接返回
- `self_timer_opts[] = {0, 2, 5, 10}` — 定时选项表

**正常模式显示** — 主页状态行（y=110）显示自拍定时 + 多重曝光数值，例如 `T5M2`、`T--M0`；不再使用旧横屏的“右上角状态栏”布局。详见 [`docs/OLED竖屏主页设计.md`](docs/OLED竖屏主页设计.md)。

### 设置菜单（menu=10）

参数驱动设计，通过 `menu_item_t` 表扩展：

| 参数 | 选项 | 只读 |
|------|------|------|
| SelfTimer | 0/2/5/10s | — |
| MultiExp | -1/0/1~10 次 | — |
| Flight | 0/1（重启生效） | — |
| IP Addr | 显示当前 IP 或 "not connected" | ✅ 只读 |

**操作逻辑**：
- 上/下键：未调整时切换参数，调整中时修改值
- 短按：切换调整模式（只读项不可进入调整）
- 长按：退出设置菜单回到 AUTO

**OLED 布局**（32×128 竖屏）：
```
标题反显 MENU
参数名两行小字（如 Self / Timer）
值用 6×12 居中（如 5s）
调整模式底部显示 EDIT
```
详见 [`docs/OLED竖屏主页设计.md`](docs/OLED竖屏主页设计.md)。

### 测光标定与曝光计算

**标定流程**：
1. OPT4001 读原始 lux → `last_lux_raw`
2. `raw_lux × METERING_ATTEN_K` → `last_lux`（校准后 lux）
3. `EV = log₂(last_lux × 2.56)` → `ev`（ISO 640 下的 EV）
4. `calc_shutter_from_ev(ev)` → `auto_shutter_pos`

**`METERING_ATTEN_K`**（`camera_main.h`）：机身窗口衰减系数。调试时对比独立测光表，调整此值直到两者 EV 一致。当前设为 256.0f。

> `metering_task` 当前每秒直接更新 `auto_shutter_pos`，没有滤波/滞回；OPT4001 初始化失败也只尝试一次（审查 R11、R13）。修改 AUTO 曝光逻辑时要考虑这两点。

**`calc_shutter_from_ev(float ev)`** — 根据校准后 EV 查表返回快门速度索引 (0-26)。

原理：F/8 镜头，`EV = log₂(64 / t_nominal)`，阈值取相邻两档 EV 中点。
`shutter_cal_x10[]`（`shutter_cal.c`）是**快门控制延时（0.1ms）**，不是标称曝光时间，与此 EV 表独立；详见 `复刻指南.md` 的快门校准章节。

| 索引 | 快门 | 标称 t(s) | EV | 阈值 EV |
|------|------|-----------|-----|------|
| 0 | 3s | 3.0 | 4.42 | ≤4.55 |
| 1 | 2.5s | 2.5 | 4.68 | ≤4.84 |
| 2 | 2s | 2.0 | 5.00 | ≤5.21 |
| 3 | 1.5s | 1.5 | 5.42 | ≤5.71 |
| 4 | 1s | 1.0 | 6.00 | ≤6.50 |
| 5 | 1/2 | 0.5 | 7.00 | ≤7.29 |
| 6 | 1/3 | 0.333 | 7.58 | ≤7.79 |
| 7 | 1/4 | 0.25 | 8.00 | ≤8.29 |
| 8 | 1/6 | 0.167 | 8.58 | ≤8.79 |
| 9 | 1/8 | 0.125 | 9.00 | ≤9.16 |
| 10 | 1/10 | 0.1 | 9.32 | ≤9.62 |
| 11 | 1/15 | 0.0667 | 9.91 | ≤10.12 |
| 12 | 1/20 | 0.05 | 10.32 | ≤10.62 |
| 13 | 1/30 | 0.0333 | 10.91 | ≤11.20 |
| ... | ... | ... | ... | ... |
| 22 | 1/1000 | 0.001 | 15.96 | ≤16.46 |
| 23 | 1/2000 | 0.0005 | 16.96 | ≤17.46 |
| 24 | 1/4000A | 0.00025 | 17.96 | ≤18.46 |
| 25 | 1/4000B | 0.00025 | 17.96 | (手动) |
| 26 | 1/8000 | 0.000125 | 18.96 | —— |

**闪光灯同步**：AUTO 模式检测到闪光灯 → 快门固定 1/30s（索引 13）。

**AUTO 模式流程**：
1. `metering_task` (1s 周期) → `auto_shutter_pos`
2. `control_task` 检测 S1T + `menu==0` → `shutter_speed = auto_shutter_pos`
3. `shutter_task` 用 `shutter_speed` 调 `get_shutter_time_x10()` 查 NVS 校准表获得控制延时

**全模式显示**（`display_manager.c`）：32×128 竖屏；AUTO 模式大字显示测光档位，ISO 小字固定为 `ISO600`，EV 在 y=95 一行显示；LUX 暂未显示（见设计文档）。`ISO600` 是页面标识，EV 计算使用 ISO 640（`×2.56`），两者差异是有意保留的，修改需重新标定。

非 AUTO 模式（BULB/TIME/MANUAL）下 `shutter_speed` 由用户手动选择，不受测光影响。

- `src/` 通过 `EXTRA_COMPONENT_DIRS` 注册为独立组件，新增 .c 文件需在 `src/CMakeLists.txt` 的 SRCS 中添加
- 新增组件依赖时注意 CMake 组件名 vs 头文件名不一致（如 `esp_ota_ops.h` → `app_update`）
- `freertos` / `esp_mac` 等基础组件自动链接，无需声明 REQUIRES
- `.vscode/settings.json` 中 `IDF_TARGET` 固定为 `esp32`
- OTA 升级前必须调用 `camera_pause()`；但当前只挂起 `control_task`，display/metering/shutter 仍可能运行（审查 R01）。在修复 R01 前，不要在拍照/吐片过程中发起 OTA
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 要求新固件在确认前调用 `esp_ota_mark_app_valid_cancel_rollback()`；若确认前崩溃/重启，Bootloader 会回滚。当前在 `app_main` 最开头确认（R18），修改初始化顺序时需一并评估
- 若 OTA 后重启仍运行旧固件，检查串口日志中的 "Running partition: xxx, state: x" 确认启动分区和回滚状态
- 修改 OTA、快门时序、显示/I2C、Web handler 后，回头更新 [`docs/review/2026-11-10-code-review.md`](docs/review/2026-11-10-code-review.md) 中对应条目的状态
