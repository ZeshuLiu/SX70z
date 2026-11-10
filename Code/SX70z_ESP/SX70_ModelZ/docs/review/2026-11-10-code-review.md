# SX70z 固件代码审查记录

- **日期**：2026-11-10
- **审查基线**：`Working` @ `ca8716b`（`feat: 实现Terminus竖屏OLED主页与状态显示`）
- **范围**：`main/main.c`、`src/*.c` / `src/*.h`、`tests/ui_host/`
- **方法**：静态代码阅读 + 现有 host 测试对照；**未烧录、未连接实机、未读取 Flash/NVS**
- **状态**：以下条目均为审查结论，尚未修改代码

严重度定义：

| 级别 | 含义 |
|---|---|
| 高 | 可能导致执行器误动作、数据损坏、固件异常或安全风险 |
| 中 | 时序抖动、并发竞争、状态不一致或明显体验问题 |
| 低 | 可维护性、可读性、可测试性和文档一致性问题 |

## 结论摘要

当前代码的主要风险集中在五处：

1. **OTA 暂停范围不足**：只挂起 `control_task`，display/metering/shutter 仍可能访问 Flash 或 I2C（R01）。
2. **执行器等待没有超时**：S3/S5/S1T 任一处卡住，电机或电磁铁可能长期通电，快门可能一直打开（R02）。
3. **`vTaskSuspend()` 与 I2C 锁冲突**：被挂起的任务可能正持有 I2C1 总线锁，导致倒计时/显示最多阻塞约 1 秒（R03）。
4. **Web 缓冲区边界不严谨**：首页长度只剩约 70 字节余量，截断时会越界读；校准表单在特定长度下越界写（R04、R05）。
5. **校准保存/重置失败仍显示成功**：NVS 写失败后页面仍标记已校准，Reset 失败也会在重启后恢复旧值（R06）。

建议先修 **R01–R07**，再做 **R08–R12** 的时序/I2C 优化，其余按需处理。

---

## 高优先级

### R01 OTA 只暂停 `control_task`，没有停止所有 Core 1 任务

- **位置**：`src/camera_main.c:574-584`（`camera_pause`）、`src/ota_web.c:123`、`src/camera_main.c:227-239`
- **现象**：`camera_pause()` 只 `vTaskSuspend(control_task_handle)`；`display_task`（200ms 周期 I2C 刷屏）、`metering_task`（1s 周期 I2C 读）和 `shutter_task` 没有停止。
- **影响**：
  - Flash 写期间代码取指/I2C ISR 会被长时间影响，显示和测光可能超时。
  - 如果 OTA 开始时正好处于曝光/吐片流程，强时序动作会被打断，属于机械与曝光风险。
  - `camera_pause()` 也没有等待 `shutter_task` 空闲，存在“正在拍照时开始写 Flash”的窗口。
- **建议**：
  - 改成协作式暂停：任务在循环安全点检查 `ota_in_progress` 后主动阻塞。
  - 至少暂停 control/display/metering 三个任务，并等待 `shutter_task` 进入阻塞态。
  - OTA 期间设置全局互锁，禁止新的 `xTaskNotifyGive(shutter_task_handle)`。

### R02 S3/S5/S1T 等待全部没有超时

- **位置**：
  - 反光板 `S3`：`src/camera_main.c:408-410`
  - 吐片 `S5`：`src/camera_main.c:435-437`
  - B 门释放 `S1T`：`src/camera_main.c:301-308`
  - T 门第一次释放/第二次按下：`src/camera_main.c:321-323`、`333-345`
  - 单次曝光结束/吐片后的释放等待：`src/camera_main.c:364`、`444-446`
- **现象**：所有 `while` 都是无限等待，部分还是 `delay_us()` 忙等。
- **影响**：
  - S3 卡住：电机一直转。
  - S5 卡住（例如没有胶片）：吐片电机一直转。
  - S1T 卡住：快门一直开、`shutter_task`（prio 10）独占 Core 1，控制/显示任务被饿死。
- **建议**：为每个等待增加毫秒级超时；超时后强制 `MOTOR_PIN=0`、`sol1_release()`、`sol2_disengage()`，记录错误状态并在 OLED 上提示。非曝光关键路径的等待改为 `vTaskDelay()`，让低优先级任务有机会运行。

### R03 `vTaskSuspend()` 可能挂起正持有 I2C 锁的任务

- **位置**：`src/camera_main.c:227-239`、`246-253`、`263`、`356-359`
- **现象**：`suspend_noshut_task()` 直接从快门任务挂起 control/display/metering。display 任务可能正处于 I2C1 传输中并持有 legacy I2C 驱动的总线锁。
- **影响**：随后快门任务在自拍/B/T 倒计时里调用 `display_show_countdown()`，可能等 I2C 锁最长约 1 秒；倒计时会延迟或刷新失败。
- **建议**：不要让其他任务从外部 `vTaskSuspend` 显示任务；改为“请求暂停 + 任务在安全点自行阻塞”。或者暂停前先获取/确认 I2C1 总线空闲。
- **关联**：R01 的 OTA 暂停也应采用同样方式。

### R04 `GET /` 页面超长时会越界读

- **位置**：`src/ota_web.c:88-105`
- **现象**：`char page[2048]`，当前内容约 **1978 字节，只剩约 70 字节余量**。`snprintf` 截断后返回“本应写入的长度”，代码仍然 `httpd_resp_send(req, page, len)`。
- **影响**：一旦页面再加一行、设备型号变长或文案调整，就会按超过 2048 的长度读取栈缓冲区，可能发送垃圾数据或崩溃。
- **建议**：`if (len >= sizeof(page)) len = sizeof(page) - 1;`，或改成 `static` 缓冲区 / `httpd_resp_send_chunk()` 分块发送；同时把长度检查加入 host 测试。

### R05 校准表单缓冲区可能越界写 1 字节

- **位置**：`src/ota_web.c:285-303`
- **现象**：`char body[1024]`，校验条件为 `total <= 1024`；随后 `body[received] = '\0'`。当 `total == 1024` 时写入 `body[1024]`，越过数组末尾。
- **影响**：恶意或异常请求可破坏相邻栈数据；正常表单约 270 字节，不会触发，但属于明确的边界错误。
- **建议**：改为 `char body[1025]`，或把校验改为 `total >= sizeof(body)` 时拒绝。

### R06 校准保存/重置失败后仍显示成功

- **位置**：`src/ota_web.c:327-331`、`232-239`
- **现象**：
  - `shutter_cal_save()` 的返回值没有检查，随后无条件 `shutter_cal_set_valid(true)`。
  - Reset 路径忽略 `shutter_cal_erase()` 的错误。
- **影响**：
  - NVS 写失败时页面显示“Calibrated”，重启后实际还是旧值/默认值。
  - 擦除失败时页面显示已重置，重启后旧校准数据又回来了。
- **建议**：只有 `shutter_cal_save()` 返回 `ESP_OK` 才设置 valid；`shutter_cal_erase()` 失败时返回错误页面/状态码，不要静默成功。可同时验证 27 个字段是否完整解析。

### R07 B/T 门 + 闪光时 SOL2 长期 100% 通电

- **位置**：`src/camera_main.c:255-259`、`359-362`；占空比定义见 `src/camera_main.h:19-20`
- **现象**：只要 `use_flash`，曝光前 `sol2_engage()` 把光圈电磁铁设为 100%，一直保持到曝光结束才 `sol2_disengage()`。
- **影响**：普通闪光曝光几十毫秒没问题；B/T 门可能开几分钟甚至更久，线圈会持续满载发热、耗电。
- **建议**：参考 SOL1 的 `FULL → HOLD` 策略，给 SOL2 增加保持占空比；或只在闪光触发窗口内吸合。修改前先确认 SOL2 机械保持特性。

---

## 中优先级

### R08 S1/S2 去抖按“循环次数”而不是时间，且 S1 语义反直觉

- **位置**：`src/gpio_inputs.c:40-62`、`269-312`；调用点 `src/camera_main.c:530-554`
- **现象**：
  - `s1t` 和 `flash_debounce` 都是每次调用加减 1，默认假设 control 循环 30ms。
  - display 每 200ms 会占 I2C1 约 46ms（100kHz 刷 512 字节），control 读 PCF8575 时要等总线锁，实际循环周期会在 30–80ms 间抖动，去抖时间也随之变化。
  - `debounce_read_s1pin()` 松开时返回 `true`，按住约 150ms 后才返回 `false`；`control_task` 用 `if (!s1t_pressed)` 触发快门。
- **影响**：快门触发延迟约 150ms；快速轻点可能被忽略；开机时 S1T 卡住会直接触发一张。
- **建议**：改成基于 `esp_timer_get_time()` 或 tick 的毫秒去抖；明确区分“稳定按下”和“释放”，用下降沿触发；增加开机未释放保护。

### R09 I2C1 实际 100kHz，可评估 400kHz

- **位置**：`src/pin_init.c:69-78`（`clk_speed = 100000`）
- **现状**：整帧 512 字节在 100kHz 下约 46ms；400kHz 约 12ms。
- **建议**：确认上拉电阻和走线后评估 400kHz，可直接缓解 R03/R08 的总线占用和按键延迟。注意 AGENTS/文档里存在 400kHz 与 100kHz 两种说法，应以实测为准。

### R10 显示每 200ms 全量重绘 + 全量 I2C 刷屏

- **位置**：`src/camera_main.c:187-193`、`src/ssd1306.c:184-186`
- **现象**：无论内容是否变化，每 200ms 清屏、重绘并发送完整 512 字节。
- **建议**：增加 `dirty` 标志，或保存一份“上次已发送”的帧缓冲，内容相同则跳过 `ssd1306_show()`。空闲时能显著减少 I2C 占用和功耗。

### R11 测光没有滤波，AUTO 档位在阈值附近会跳

- **位置**：`src/camera_main.c:207-224`、`155-183`
- **现象**：每秒直接把 `auto_shutter_pos` 更新为 `calc_shutter_from_ev()` 的结果。
- **影响**：EV 在档位边界抖动时，OLED 快门速度和实际拍摄档位会来回跳。
- **建议**：对 `ev` 做 EMA/中值滤波，并对 `auto_shutter_pos` 加滞回（例如 ±1/6 EV 才换档）；显示和触发使用同一份滤波结果。

### R12 OPT4001 读取可能跨转换边界拼接

- **位置**：`src/opt4001.c:190-208`
- **现象**：先读 `RESULT_MSB`，再读 `RESULT_LSB`，两次独立事务；两次之间若完成一次转换，MSB/LSB 可能来自不同样本。
- **建议**：从 `0x00` 起一次 burst 读 3 字节（利用自增），或读取 FLAGS 确认 conversion-ready；`last_range_index` 目前未被使用，可删除或暴露为诊断信息。

### R13 OPT4001 初始化失败后永久放弃

- **位置**：`src/camera_main.c:196-204`、`src/opt4001.c:135-177`
- **现象**：`metering_task` 初始化失败后直接 `vTaskDelete(NULL)`；本次开机不再重试。
- **影响**：上电瞬间传感器异常时，AUTO 一直使用初始的 1/2s，用户只能重启。
- **建议**：失败后退避重试（例如 1s/2s/5s），并设置 `metering_ok` 状态供显示区分“没测光”和“EV 为 0”。

### R14 WiFi 重连没有退避，也没有失败回退

- **位置**：`main/main.c:57-61`
- **现象**：每次 `WIFI_EVENT_STA_DISCONNECTED` 立即 `esp_wifi_connect()`。
- **影响**：密码错误或 AP 不在时高频重连；没有自动重新配网入口。
- **建议**：指数退避（1/2/4/…秒），连续失败 N 次后重新进入 provisioning，或在设置菜单提供“重新配网”。

### R15 `wifi_enabled` / `ble_enabled` 表示“服务已启动”，不是“已连接”

- **位置**：`main/main.c:51-56`、`src/display_manager.c:158-167`
- **现象**：`STA_START` 就置 `wifi_enabled = true`；OLED 显示 `W+` 时可能还没有 IP。
- **建议**：增加 `wifi_connected`（GOT_IP/DISCONNECTED 维护）或直接用 `ip_str[0]`；BLE 同理区分“广播中/已连接”。

### R16 `ip_str` 跨任务读写没有保护

- **位置**：`main/main.c:62-66`、`src/display_manager.c:189-200`
- **现象**：WiFi 事件任务用 `snprintf` 写入，显示任务逐字符读取，可能读到半个 IP。
- **建议**：双缓冲 + 原子交换索引，或短临界区拷贝到显示任务局部缓冲。

### R17 `volatile struct` 不等于线程安全

- **位置**：`src/camera_main.h:53-74`、`src/display_manager.h:7`、`src/gpio_inputs.c:93-95`
- **现象**：整个 `camera_state_t` 被声明为 `volatile`，但仍没有多字段一致性保证；传给 `const camera_state_t *` 会丢掉 volatile，编译器可能告警。`menu_item_idx` / `menu_adjust` 也是无保护的跨任务全局变量。
- **建议**：去掉“用 volatile 当锁”的思路；跨任务字段用临界区/互斥保护，或让显示/快门任务取一份快照。把菜单全局变量收进状态结构体。

### R18 OTA 回滚确认太早

- **位置**：`main/main.c:126-143`
- **现象**：检测到 `PENDING_VERIFY` 后立即 `esp_ota_mark_app_valid_cancel_rollback()`，发生在硬件、显示、网络初始化之前。
- **影响**：能启动但功能损坏的固件会被永久确认，失去自动回滚机会。
- **建议**：ESP-IDF 的回滚不是定时器机制，可以把确认延后到控制任务、显示、WiFi 初始化成功之后；中途崩溃仍然会回滚。

### R19 OTA 接收把 socket 超时当致命错误

- **位置**：`src/ota_web.c:141-161`
- **现象**：`httpd_req_recv()` 返回任何负值都 `esp_ota_abort()`。
- **影响**：慢速手机上传大固件时，`HTTPD_SOCK_ERR_TIMEOUT` 会导致升级被 400 中断。
- **建议**：对 `HTTPD_SOCK_ERR_TIMEOUT` 重试/继续；结合 `req->content_len` 判断是否提前结束。

### R20 `esp_ota_set_boot_partition` 后没有真正校验

- **位置**：`src/ota_web.c:172-187`
- **现象**：只打印 `esp_ota_get_boot_partition()` 的结果，不判断是否等于 `part`。
- **建议**：不一致时直接报错并保持旧启动分区，不要重启。

### R21 多重曝光的“中止”操作不直观

- **位置**：`src/camera_main.c:420-428`
- **现象**：设置 `multi_exp_remain = -1` 后，还需要再按一次 S1T 产生一次 notify，才会走到 `break`；否则快门任务一直阻塞等待。
- **建议**：允许长按/超时中止，或在 OLED 上提示“再按一次结束”。同时明确 `-1` 只在首张前生效还是任意时刻生效。

### R22 自拍倒计时会累积漂移

- **位置**：`src/camera_main.c:246-253`
- **现象**：每秒先刷屏再 `delay_us(1000000)`，刷屏 I2C 时间会加到每一秒上。
- **影响**：10 秒倒计时大约慢 0.4–0.5 秒。
- **建议**：用绝对目标时间 `esp_timer_get_time()`，每次计算剩余时间。

---

## 低优先级

### R23 初始化返回值大量未检查

- **位置**：`src/camera_main.c:481-528`、`src/pin_init.c:65-78`、`main/main.c:195`、`src/ota_web.c:346-370`
- **现象**：`gptimer_new_timer`、`ledc_timer_config` / `ledc_channel_config`、`xTaskCreatePinnedToCore`、`i2c_driver_install`、`esp_netif_create_default_wifi_sta`、`httpd_register_uri_handler` 的返回值均未判断。
- **影响**：失败后可能空指针解引用（例如 `g_delay_timer`），或功能静默缺失。
- **建议**：至少记录错误并降级（例如无显示时继续运行），关键路径用 `ESP_ERROR_CHECK`。

### R24 PCF8575 错误状态与“全部松开”不可区分

- **位置**：`src/pcf8575.c:31-42`、`src/gpio_inputs.c:99-111`、`272-279`
- **现象**：`pcf8575_read()` 失败返回 `0xFFFF`，按键状态机把它当成全部松开。若此时正按着“按下”键，会产生假的释放沿，可能误触发长按菜单。
- **建议**：改成返回 `esp_err_t` + 输出参数；失败时跳过本次按键状态更新。另把 `read_3d_button_pins()` 移到 100ms 防抖判断之后，避免每 30ms 白读一次 I2C。

### R25 `update_mode_display()` 每 30ms 无条件重算

- **位置**：`src/camera_main.c:530-533`、`src/gpio_inputs.c:113-140`
- **现象**：主循环和每个按键处理函数都调用，实际只在 `menu/time_mode/shutter_speed` 变化时需要。
- **建议**：只在状态变化处调用，或比较后更新。

### R26 显示层解析快门速度字符串过于脆弱

- **位置**：`src/display_manager.c:79-109`
- **现象**：通过判断 `"1/"`、字符串末尾 `A/B` 来拆分标签。
- **建议**：由 `shutter_cal` 暴露结构化信息（索引、标签、是否分数、后缀），显示层不再解析展示字符串。

### R27 `centered()` 静默丢弃超宽文本；ISO600/640 不一致

- **位置**：`src/display_manager.c:14-20`、`137`；`src/camera_main.c:211-214`
- **现象**：文本宽度超过 32 像素时什么都不画，调试时难以发现；显示固定 `ISO600`，而 EV 计算按 ISO 640。
- **说明**：`ISO600` 是设计文档明确保留的页面标识，不算当前 bug；若要统一必须重新标定。
- **建议**：`centered()` 至少截断/缩写或加日志；在文档中注明 ISO 标识与计算基准的差异。

### R28 `devinfo.build_time` 可能不是本次构建时间

- **位置**：`src/devinfo.c:13`
- **现象**：使用 `__DATE__` / `__TIME__`，`devinfo.c` 不重新编译就不会更新。
- **建议**：用 CMake `-DBUILD_TIME=...` 注入，或把构建时间放在生成的版本头文件中。

### R29 关键配置只存在于本地 `sdkconfig`

- **位置**：`sdkconfig.defaults` 与本地 `sdkconfig`（后者已 gitignore）
- **现象**：本地有 `CONFIG_ESP_TASK_WDT_CHECK_IDLE_TASK_CPU1 is not set`、`CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ_240=y`、`CONFIG_COMPILER_OPTIMIZATION_SIZE=y`，但 `sdkconfig.defaults` 中没有。
- **影响**：干净构建可能回到 160MHz、默认优化和 CPU1 idle WDT 检查；长曝光/自拍中的 `delay_us` 忙等可能触发 TWDT 警告，时序也与发布固件不同。
- **建议**：把这些关键项写入 `sdkconfig.defaults`，保证可复现构建。

### R30 注释/文档与代码不一致

- **位置**：`src/pin_init.c:56`（注释写 I2C0 是 PCF8575，实际 I2C0 是 OPT4001）；`AGENTS.md` I2C1 400kHz；`Camera.md` 与 `复刻指南.md` 对 `x10` 单位描述不同。
- **说明**：本次文档更新已修正 `AGENTS.md` 的 I2C1 速度、初始化顺序和 `x10` 口径；`pin_init.c` 的注释仍是旧描述，需要代码侧修正。
- **建议**：统一为“I2C0=OPT4001、I2C1=SSD1306+PCF8575、当前 100kHz”；`x10` 统一按 `复刻指南.md` 的口径描述为“控制延时，不是曝光时间”。

### R31 测试覆盖偏窄

- **位置**：`tests/ui_host/test_ui.c`
- **现状**：只覆盖绘图/布局。
- **建议**：增加 `calc_shutter_from_ev` 27 档边界、`time_mode_next/prev`、菜单状态机、OTA 页面长度、表单缓冲区边界等 host 测试；R04/R05 这类问题很容易被测试提前发现。

### R32 时序魔法数分散

- **位置**：`src/camera_main.c:259`、`276`、`289`、`292`、`314`、`354`、`356`、`364`
- **现象**：`47000`、`32000`、`15000`、`18000`、`30000`、`10000` 等直接写在流程里。
- **建议**：命名成常量或时序表，标注来源（SX-70 规格/实测），方便校准和审查。

### R33 Web 端点无认证

- **位置**：`src/ota_web.c:346-370`、`main/main.c:215-217`
- **现象**：OTA、校准页面没有认证；BLE PoP 硬编码为 `sx70z123`。
- **影响**：同一局域网内的任何人都可以刷固件或改校准值。
- **建议**：至少增加一次性 token / 物理按键确认；PoP 改为每台设备独立或首次配网时生成。若产品只在可信网络使用，也应明确记录这一限制。

---

## 已确认的设计取舍（不要误改）

以下内容看起来像 bug，但当前文档/代码表明是有意设计，修改前需要实机确认：

1. **`shutter_cal_x10` 是控制延时，不是曝光时间**：`复刻指南.md` 明确说明 240 表示 24ms 控制延时，不代表 1/8000 的实际曝光时间。`Camera.md` 的“实际延时”表述容易误解，建议统一为“控制延时”。
2. **闪光分支固定等待 47ms 再触发**：`gap = max(shutter_delay_x10 - 470, 0)`，因此比 1/20 更快的档位在闪光路径下实际会变成约 48ms。`Camera.md` 已描述该行为；若不符合预期，应先改时序设计，不要只改 UI。
3. **AUTO + 闪光固定 1/30s（索引 13）**：`control_task` 和显示层都按这个规则处理。
4. **OLED 显示 `ISO600`**：设计文档明确保留该页面标识，本次不修改曝光计算或 ISO 标定。
5. **实际贴装 PCF8574TS/3，但代码使用 PCF8575 驱动连续读两字节**：作者已确认现有固件适配实际芯片；不要仅凭型号差异直接“修正”。
6. **I2C1 当前 100kHz**：代码为准；400kHz 只是待验证的优化项。

## 建议修复顺序

1. **第一批（安全/可靠性）**：R01、R02、R03、R04、R05、R06、R07。
2. **第二批（时序/总线）**：R08、R09、R10、R11、R12、R13。
3. **第三批（状态/网络/OTA 健壮性）**：R14–R22。
4. **第四批（可维护性/测试）**：R23–R33，并补 host 测试。

## 验证清单（修复后）

- [ ] OTA：空闲状态升级、曝光中发起升级、吐片中发起升级，均不出现执行器误动作或重启异常。
- [ ] 拔掉/短接 S3、S5、S1T 模拟卡死，确认超时后电机停、电磁铁释放、快门关闭。
- [ ] 自拍 10s 倒计时误差 < 0.1s；B/T 长曝光期间显示与按键仍可响应（或明确提示忙）。
- [ ] 慢速上传大固件（限速/手机）不被 `HTTPD_SOCK_ERR_TIMEOUT` 中断。
- [ ] `GET /` 在设备型号/文案加长后仍不越界；校准表单发送 1024 字节边界不越界。
- [ ] 模拟 NVS 写失败/擦除失败，页面不能显示“已校准/已重置”。
- [ ] B/T + 闪光连续工作，确认 SOL2 温升和供电在可接受范围。
- [ ] 评估 I2C1 400kHz 后的刷屏时间、按键延迟和稳定性。
- [ ] 干净构建（只依赖 `sdkconfig.defaults`）确认 CPU 频率、优化级别、TWDT 设置与发布固件一致。

## 维护约定

- 修复某条后，在本文件对应标题下标记 `状态：已修复（commit）`，并更新摘要。
- `AGENTS.md` 顶部“代码审查”小节应保持与本文件一致。
- 新增或修改 OTA、快门时序、显示/I2C、Web handler 时，至少复查 R01–R07。
