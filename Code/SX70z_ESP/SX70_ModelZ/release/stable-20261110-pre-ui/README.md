# 竖屏 UI 修改前的稳定基线

归档日期：2026-11-10。按用户要求保留当前版本，供 UI 修改后回退。

- 源码提交：`55b2c1b9456e876e90468fb5e3b1393696ce7c12`。
- Git 标签：`stable-20261110-pre-ui`。
- 原分支：`Working`；UI 开发分支：`codex/oled-portrait-ui`。
- 设备信息版本：1.6.1。
- 构建环境：ESP-IDF 5.5.2、ESP32、Xtensa GCC 14.2.0。

## 备份内容

| 文件 | 内容 |
|---|---|
| `source-55b2c1b.zip` | 上述提交中 SX70_ModelZ 子目录的 Git 源码归档，包含原发布文件和设计文档 |
| `sdkconfig.snapshot` | 开始修改 UI 前的本机配置原件 |
| `sdkconfig.effective` | 用冻结源码重新构建后，由 IDF 生成的有效配置 |
| `vscode-settings.snapshot.json` | 当时的 VS Code 项目配置；其中选择的是 6.1，**不用于本次构建** |
| `published-v1.6.1-20260624.bin` | 原 `release/SX70Z_Ver1_6_1_build20260624222406.bin` 的原样备份 |
| `SX70_ModelZ.bin` | 2026-11-10 用冻结源码和 5.5.2 重新构建的应用镜像，956544 bytes |
| `bootloader/bootloader.bin` | 同次构建的引导程序 |
| `partition_table/partition-table.bin` | 同次构建的分区表 |
| `ota_data_initial.bin` | 同次构建的初始 OTA 数据 |
| `flash_args` / `flasher_args.json` | 同次构建的烧录参数，文件路径在归档中可解析 |
| `SHA256SUMS.txt` | 归档文件的 SHA-256 校验值 |

重新构建使用独立解压目录，未使用已经修改的 UI 源码。生成器自动从上级仓库得到的 app descriptor 版本含 `stable-20261110-pre-ui-dirty`；实际应用源码来自固定提交，设备信息中的版本仍为 1.6.1。分区检查通过：应用 0xe9880 bytes，最小 OTA 分区 0x1a9000 bytes。

## 验证范围

冻结源码已编译通过。原发布文件与重新构建文件分开存放，不能仅按版本号认定它们逐字节相同。

“稳定基线”是用户指定的归档名称；本次未重新进行实机稳定性测试，未连接设备、读取设备 Flash/NVS 或烧录。此归档不是相机当前 Flash 的全量读取备份，也不包含设备里的 Wi-Fi 凭据和快门校准 NVS。

后续若要回到原源码，可从 `stable-20261110-pre-ui` 新建恢复分支。重新构建时使用 ESP-IDF 5.5.2 和备份配置；不要直接套用快照中指向 6.1 的 VS Code 设置。
