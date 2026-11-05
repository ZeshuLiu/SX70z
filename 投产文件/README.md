# SX70z 投产资料（待核对）

主控板选用 **3.5** Gerber；机身控制器、机身 FPC 和外壳文件均按作者确认的投产资料收集。

| 部件 | 文件 | 用途和注意事项 |
| --- | --- | --- |
| 主控板 | `主控板/Gerber_SX70z_3.5.zip` | 3.5 版 PCB 制造文件，含四层铜层及钻孔 |
| 主控板 | `主控板/bom.csv`、`positions.csv` | 3.5 主控板 BOM 和贴装坐标，作者确认投产目录资料正确 |
| 机身控制器 | `机身控制器/BodyControllerForSX70z.zip` | PCB 制造文件 |
| 机身控制器 | `机身控制器/bom.csv`、`positions.csv` | 机身控制器 BOM 和贴装坐标，U1 型号已按实际贴装修正 |
| 机身 FPC | `机身FPC/Gerber_SX70_Model_Z_FPC_2026-05-08.zip` | 软板制造文件，含补强层等信息；具体工艺须与厂家确认 |
| 外壳 | `外壳/` | 外壳 STL、3MF 和垫片 STL；打印前确认尺寸与所选机身板匹配 |

## 下单前必须核对

- 使用 Gerber 查看器检查板框、层叠、钻孔、方向及尺寸；原 README 要求 PCB 厚度小于 0.8 mm，具体板厚、铜厚及 FPC 补强须按实物空间和厂家工艺确定。
- 机身控制器 DISP1 为焊接的 0.91 英寸 OLED 裸屏（[购买链接](https://item.taobao.com/item.htm?id=665792766196)）；J1 焊接机身 FPC，对应本目录机身 FPC 文件。SW3 使用[大头拨轮](https://item.taobao.com/item.htm?id=599059037308&skuId=4177733860764)。

复刻操作与首次调试参见固件目录的 `复刻指南.md`。
