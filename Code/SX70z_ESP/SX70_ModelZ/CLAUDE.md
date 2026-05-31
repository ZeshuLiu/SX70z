# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Polaroid SX-70 相机控制器，基于 ESP32-PICO-V3 (4MB Flash, 无 PSRAM)，运行 ESP-IDF v5.5.2。

- **硬件**: 自绘 PCB，ESP32-PICO-V3、闪光灯、快门/光圈控制、PCF8575 I2C GPIO 扩展、SSD1306 OLED、OPT4001 环境光传感器
- **功能**: 自动测光曝光、B/T/手动模式、3D 按键菜单、BLE + WiFi 配网、HTTP OTA 升级
- **仓库**: `SX70z` — 独立固件仓库，硬件设计在 `../Hardware/` 下
- **ESP-IDF**: `D:\ESPIDF\v5.5.2\esp-idf` / **工具链**: `D:\ESPIDF_TOOL`

## Build & Flash

```bash
idf.py build                      # 编译（build/ 下同时生成带时间戳的副本 bin）
idf.py -p <PORT> flash            # 烧录 (UART, QIO 80MHz)
idf.py -p <PORT> monitor          # 串口监视 (UART0, 115200 baud)
idf.py -p <PORT> erase-flash flash monitor
```

## Architecture

### 核心设计原则

- **双核隔离**: Core 0 跑所有非控制逻辑（WiFi/BLE/HTTP/日志），Core 1 只跑时序敏感的相机控制。**与控制无关的代码一律放 Core 0**
- **非阻塞初始化**: WiFi 配网、BLE 连接在后台任务运行，不阻塞主控
- **事件驱动**: WiFi/IP 状态变更通过 `esp_event` 回调通知
- **OTA 安全**: OTA 写 Flash 前挂起 Core 1 控制任务（`camera_pause`），完成后恢复或重启

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

**Shutter Task 独占机制**：优先级最高 + 忙等不释放 CPU，保证时序不受其他任务抢占。通过 `xTaskNotifyGive` 触发，执行完回到 `ulTaskNotifyTake` 阻塞。未来快门/电机等几十秒的强时序工作均在此任务完成。

### 目录结构

```
SX70_ModelZ/
├── main/
│   ├── main.c              # WiFi/BLE 初始化，启动控制任务，触发 OTA Web
│   └── CMakeLists.txt      # REQUIRES: nvs_flash esp_wifi esp_event esp_netif wifi_provisioning src
├── src/
│   ├── CMakeLists.txt      # REQUIRES: esp_http_server app_update driver esp_timer
│   ├── devinfo.h / .c      # 设备信息（序列号=芯片 MAC、软硬件版本）
│   ├── camera_main.h / .c  # 相机控制任务 (Core 1)，camera_state_t 类型定义，camera_pause/resume
│   ├── display_manager.h / .c # SSD1306 显示帧绘制（show_frame 适配）
│   ├── opt4001.h / .c      # OPT4001 环境光传感器驱动（I2C0, 0x44）
│   ├── ssd1306.h / .c      # SSD1306 OLED 显示驱动（I2C1）
│   ├── pcf8575.h / .c      # PCF8575 I2C GPIO 扩展（I2C1, 0x20-0x27）
│   ├── font.h              # 字体类型定义
│   ├── fonts/font5x8.h     # 5x8 像素字体
│   └── ota_web.h / .c      # HTTP 网页上传固件 OTA
├── components/             # ESP-IDF 标准组件目录（当前为空）
├── sdkconfig
├── partitions_two_ota_large.csv
└── .clangd
```

### 初始化流程

```
app_main() [Core 0]
  0. devinfo_init()                        — 读芯片 MAC 做序列号
  1. OTA 回滚检查                           — 若分区为 PENDING_VERIFY，调用
                                             esp_ota_mark_app_valid_cancel_rollback()
                                             确认固件有效（bootloader watchdog 要求）
  2. pin_init()                            — GPIO 初始化
  3. NVS 初始化                             — 存储 WiFi 凭据、BLE 绑定
  4. esp_netif + event loop                — 网络栈基础
  5. 注册 WiFi / IP / Provisioning 事件回调
  6. WiFi STA 启动 + 设置主机名 "SX70z"
  7. BLE Provisioning（非阻塞）             — 未配网则广播，已配网则直接连 WiFi
  8. xTaskCreatePinnedToCore(control_task, 1) — 启动 Core 1 控制任务（prio 8）
     └─ control_task() 内部：
        ├─ SSD1306 初始化（esp_lcd 官方驱动, 128×32） + PCF8575 初始化
        ├─ 创建 metering_task (prio 3) — OPT4001 初始化 + 1s 周期测光
        ├─ 创建 display_task (prio 5) — OLED 200ms 周期 I2C 刷屏
        ├─ 初始化 GPTimer — us 级精确定时 (1MHz, intr_priority=1 绑定 Core 1)
        ├─ 创建 shutter_task (prio 10) — 平时阻塞，xTaskNotifyGive 触发
        └─ 主循环：按键 + S2 + S1T (30ms 周期，无 I2C 阻塞)
  9. app_main 空闲循环（打印 RSSI 等辅助信息）
```

### OTA 升级流程

```
IP_EVENT_STA_GOT_IP → ota_web_start()
  → 浏览器打开 http://<ESP32_IP>
  → 选 .bin 文件上传
  → POST /update:
       camera_pause()              ← 挂起 Core 1 控制任务
       esp_ota_begin()             ← 打开 ota_1 分区
       esp_ota_write() × N         ← 逐块写入 Flash
       esp_ota_end()               ← 校验
       esp_ota_set_boot_partition(ota_1)
       esp_ota_get_boot_partition() ← 验证启动分区已正确设置
       esp_restart()               ← 重启进入新固件
    错误路径: camera_resume()      ← 恢复 Core 1 控制任务

  重启后 → app_main 步骤 1 检测到 PENDING_VERIFY → 调用
  esp_ota_mark_app_valid_cancel_rollback() 确认新固件有效，回滚取消
```

### OTA 回滚机制

`CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 启用后：

1. `esp_ota_set_boot_partition()` 将新分区标记为 `PENDING_VERIFY`
2. Bootloader 启动新固件时设置 watchdog 定时器
3. **新固件必须在 watchdog 超时前调用 `esp_ota_mark_app_valid_cancel_rollback()`**，否则：
   - Bootloader 将当前分区标记为 `INVALID`
   - 下次重启自动回滚到上一个有效分区
4. 若新固件启动期间崩溃（LoadProhibited 等），bootloader 同样自动回滚

关键调用在 `app_main` 步骤 1，位于所有初始化之前，确保 watchdog 不会意外触发。

### BLE / WiFi 技术栈

| 组件 | 选型 |
|---|---|
| BLE 协议栈 | NimBLE (VHCI) |
| WiFi 配网 | BLE Provisioning (Proof of Possession) |
| OTA | WiFi HTTP 网页上传 |
| 分区表 | `partitions_two_ota_large.csv` (ota_0 + ota_1 各 1700KB) |

### sdkconfig 关键配置

- `CONFIG_IDF_TARGET="esp32"` — PICO-V3 在 IDF 中归类为 ESP32
- `CONFIG_BT_NIMBLE_ENABLED=y` — NimBLE 协议栈
- `CONFIG_PARTITION_TABLE_TWO_OTA_LARGE=y` — 双 OTA (各 1700KB)
- `CONFIG_WIFI_PROV_BLE_SEC_CONN=y` — BLE 安全配网
- `CONFIG_WIFI_PROV_KEEP_BLE_ON_AFTER_PROV=y` — 配网后保持 BLE
- `CONFIG_BT_NIMBLE_NVS_PERSIST=y` — BLE 配对持久化
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` — OTA 失败回滚
- `CONFIG_COMPILER_OPTIMIZATION_DEBUG=y` — 开发阶段用 DEBUG
- `CONFIG_ESPTOOLPY_FLASHMODE_QIO=y` / `CONFIG_ESPTOOLPY_FLASHFREQ_80M=y` — QIO 80MHz
- `CONFIG_LOG_DEFAULT_LEVEL_DEBUG=y` — 默认日志级别 DEBUG
- BLE 设备名 `"SX70z"` / 广播名 `"SX70z_POP"` / PoP `"sx70z123"`
- WiFi 主机名 `"SX70z"`

### 日志级别约定

| 级别 | 用途 |
|---|---|
| `ESP_LOGE` | 错误（始终打印） |
| `ESP_LOGW` | 警告 |
| `ESP_LOGI` | 重要信息：WiFi 连接/断开、配网成功/失败、OTA 进度、设备信息 |
| `ESP_LOGD` | 调试细节：STA 启动、配网开始/结束、凭据收到、WiFi 未连接 |
| `ESP_LOGV` | 未启用 |

## Peripheral Hardware & I2C Bus

| 总线 | 引脚 | 速度 | 设备 |
|------|------|------|------|
| I2C0 | GPIO21/22 | 100kHz | OPT4001 (0x44) |
| I2C1 | GPIO7/8 | 400kHz | SSD1306 (0x3C), PCF8575 (0x20-0x27) |

### OPT4001 环境光传感器

- 挂 I2C_NUM_0（GPIO21/22），地址 0x44
- 初始化在 `metering_task` (Core 1, prio 3) 中调 `opt4001_init()`，由 `control_task` 创建该任务
- 自动量程模式，800ms 转换周期，连续采样 — 初始化后首次有效数据延时 900ms
- I2C 读时序：两次独立事务（写寄存器地址 → STOP → 读数据 → STOP），备选 Repeated Start 方案注释在代码中待验证
- 量程 0.001 ~ 2,200,000 lux，12 档硬件自动切换

### SSD1306 OLED

- 挂 I2C_NUM_1（GPIO7/8），默认地址 0x3C，100kHz
- **128×32 像素**（注意不是 64！）
- 基于 ESP-IDF 官方 `esp_lcd` 驱动（`esp_lcd_panel_ssd1306`），init/写屏/开关由官方库处理
- 自维护本地帧缓冲 `dev->buff`（512 bytes），绘图 API 保留：`ssd1306_draw_str/line/rect/circle` 等
- `ssd1306_show()` 通过 `esp_lcd_panel_draw_bitmap()` 全量刷新到屏幕
- 字体使用 `font5x8_font`（5x8 像素，96 字符 ASCII）

### PCF8575 I2C GPIO 扩展

- 挂 I2C_NUM_1（GPIO7/8），地址 0x20-0x27
- 16 位 GPIO，方向可逐位配置（1=输入 0=输出）
- API：`pcf8575_init/read/write/write_pin/read_pin/set_input/set_output`

### 快门控制 (Shutter Expose)

快门曝光时序，运行在 `shutter_task` (prio 10)：

```
1. 关快门 (SOL1 high, 30ms)
2. 电机启动 → 等 S3 反光板就位 → 电机停
3. Y delay (18ms, 闪光模式 + SOL2 光圈)
4. 曝光 (根据 mode):
   '1' Normal  — 开快门 → delay_us(时间×100) → 关快门
   '0' Flash   — 开快门 → 47ms → FF 脉冲 → gap 延时 → 关快门
   'B' Bulb    — 开快门 → 等 S1T 释放
   'T' Time    — 开快门 → 等 S1T 释放 → 等 S1T 再按下
5. 关快门 + 30ms + 18ms
6. 光圈归位 (闪光模式)
7. 电机吐片 → 等 S5 胶片检测
8. 等 S1T 释放防连拍
```

- 快门速度表：27 档 (`"3s"` ~ `"1/8000"`)，`shutter_times_x10[]` 为校准后实际延时 (0.1ms 单位)
- SOL1/SOL2 通过 LEDC PWM 控制（31.25kHz, 8-bit），100% 吸合 / 40% 保持
- `#define HAS_FOCUS 0` 控制对焦功能编译开关，当前为 TODO 占位
- S1T 去抖：`debounce_read_s1pin()` 计数器方式 (`S1_DEBOUNCE_COUNT=5`)

## Development Notes

### camera_state_t 参数结构体

相机所有状态集中在 `camera_state_t camera_state`（`camera_main.c` 定义，`camera_main.h` extern 声明），含嵌套子结构：

```
camera_state_t
├── if_display                  OLED 是否可用（可用时 LED 保持熄灭）
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
├── menu / cam_mode / shut_mode / shutter_speed  曝光/模式参数
└── test_led_level              调试 LED 电平（仅 OLED 不可用时闪烁）
```

### 3D 按键处理（PCF8575）

通过 PCF8575 I2C GPIO 扩展读取 3 个物理按键（上/下/按下）。

**按键引脚**（`PIN.h`）：`PCF_BUTTON3D_DOWN=10`, `PCF_BUTTON3D_UP=8`, `PCF_BUTTON3D_PUSH=9`

**核心函数**：
- `read_3d_button_pins(char *result)` — 读 PCF8575 16 位状态，提取按键位到 `"101"` 字符串
- `button3d_handler()` — 100ms 防抖 + 下降沿/上升沿检测 + 长短按区分（>1000ms）
- `down_button_call()` — M 档快门速度递减（环形，0→26→0）
- `up_button_call()` — M 档快门速度递增
- `push_button_short()` — 菜单循环：AUTO(0)→BULB(1)→TIME(2)→MANUAL(3)→AUTO(0)
- `push_button_long()` — 进入/退出自拍定时（menu=10）
- `update_mode_display()` — 根据 `menu` 更新 `cam_mode` 字符串

**菜单结构**：
| menu | 模式 | cam_mode | 快门来源 |
|------|------|----------|---------|
| 0 | AUTO | "AUTO" | auto_shutter_pos (测光自动) |
| 1 | BULB | "B" | B 门（S1T 释放结束） |
| 2 | TIME | "T" | T 门（S1T 按两次） |
| 3 | MANUAL | 快门速度字符串 | 手动选择（上/下键） |
| 10 | 自拍定时 | "---" | 禁止拍摄 |

**主循环调用**：`control_task` 每 100ms 调 `button3d_handler()`，在闪光灯检测之前。

### us 级精确定时 (`delay_us`)

基于 GPTimer 实现（`camera_main.c:23-49`）：
- `g_delay_timer` — GPTimer handle (1MHz = 1μs, one-shot alarm, 无自动重载)
- `delay_timer_cb` — `IRAM_ATTR` ISR 回调设 `g_timer_done` 标志位
- **`intr_priority = 1`** 确保 ISR 绑定到 Core 1（`priority=0` 走默认分配，不保证核亲和）
- `delay_us(uint32_t us)` — 重设 alarm + 启动 timer + 忙等，期间不释放 CPU
- 精度：±2μs（硬件定时器 ISR 同核直连，零跨核开销）
- IRAM 安全：驱动内部 `GPTIMER_INTR_ALLOC_FLAGS` 默认含 `ESP_INTR_FLAG_IRAM`

### Shutter Task 使用方式

```c
// 触发快门动作（可在 control_task 或其他任务中调用）
xTaskNotifyGive(shutter_task_handle);
// shutter_task 立即抢占 Core 1，执行完回到阻塞态
```

### 测光标定与曝光计算

**标定流程**：
1. OPT4001 读原始 lux → `last_lux_raw`
2. `raw_lux × METERING_ATTEN_K` → `last_lux`（校准后 lux）
3. `EV = log₂(last_lux × 2.56)` → `ev`（ISO 640 下的 EV）
4. `calc_shutter_from_ev(ev)` → `auto_shutter_pos`

**`METERING_ATTEN_K`**（`camera_main.h`）：机身窗口衰减系数。调试时对比独立测光表，调整此值直到两者 EV 一致。当前设为 256.0f。

**`calc_shutter_from_ev(float ev)`** — 根据校准后 EV 查表返回快门速度索引 (0-26)。

原理：F/8 镜头，`EV = log₂(64 / t_nominal)`，阈值取相邻两档 EV 中点。
`shutter_times_x10[]` 为校准后实际延时，与此 EV 表无关。

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
3. `shutter_task` 用 `shutter_speed` 查 `shutter_times_x10` 表获得曝光时间

**全模式 EV/LUX 显示**（`display_manager.c`）：OLED 右下角显示 `EV13.6 L430.12`，LUX 小数位自适应（6 位总宽，整数位多则小数少）。快门速度大字在 AUTO 模式实时反映测光结果。

非 AUTO 模式（BULB/TIME/MANUAL）下 `shutter_speed` 由用户手动选择，不受测光影响。

- `src/` 通过 `EXTRA_COMPONENT_DIRS` 注册为独立组件，新增 .c 文件需在 `src/CMakeLists.txt` 的 SRCS 中添加
- 新增组件依赖时注意 CMake 组件名 vs 头文件名不一致（如 `esp_ota_ops.h` → `app_update`）
- `freertos` / `esp_mac` 等基础组件自动链接，无需声明 REQUIRES
- `.vscode/settings.json` 中 `IDF_TARGET` 固定为 `esp32`
- OTA 升级时必须先 `camera_pause()` 挂起 Core 1，避免 Flash 写冲突导致 LoadProhibited
- `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` 要求固件启动后调用 `esp_ota_mark_app_valid_cancel_rollback()`（在 `app_main` 步骤 1 中处理），否则 bootloader watchdog 会触发回滚
- 若 OTA 后重启仍运行旧固件，检查串口日志中的 "Running partition: xxx, state: x" 确认启动分区和回滚状态
