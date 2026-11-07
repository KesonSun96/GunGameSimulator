# 硬件设计

> 默认脚踏已迁移至 STM32F103C8T6。当前目标的分层、接线、构建、测试和移植规则见 [STM32 脚踏指南](STM32_PEDAL.md)。下文 ESP32 脚踏内容仅适用于保留的旧后端。

## BOM 与供电

枪体：ESP32-S3-DevKitC-1 N16R8 或引脚等效开发板、ICM-42688-P 四线 SPI 模块、8 个微动/按钮、LED 与 330 Ω 电阻、USB 数据线、刚性外壳。脚踏：另一块 S3 N16R8、4 个机械开关、LED、承力结构。

原型使用开发板 USB 供电与板载 3.3 V 稳压。IMU 使用 3.3 V，GPIO 不接 5 V。模块是否带稳压/电平转换以其原理图为准。VDD/VDDIO 就近去耦，裸芯片按数据手册设计；模块先核对板载电容。

## 枪体接线

| 信号 | S3 GPIO | 备注 |
|---|---|---|
| IMU SCLK | 12 | SPI mode 0，当前 10 MHz，短线 |
| IMU SDI/MOSI | 11 | 主机输出 |
| IMU SDO/AD0 | 13 | 同一个复用引脚，SPI 下是 MISO，**不得接地** |
| IMU CS | 10 | 建议 10 kΩ 上拉 |
| IMU VDD/VDDIO | 3V3 | 与 MCU 共地 |
| IMU INT1 | 当前不接 | 驱动用 SPI 轮询 `INT_STATUS(0x2D)` 就绪位，无需 INT1；预留 GPIO9 可作后续中断 |
| 扳机 / ADS | 4 / 5 | 按下接地，内部上拉 |
| Reload / 武器1 / 武器2 | 6 / 7 / 15 | 同上 |
| 蹲伏 / Profile / Recenter | 16 / 17 / 18 | Recenter 单独为 clutch；与 Profile 同按 1 s 触发窗口校准 |
| LED | 21 | GPIO → 电阻 → LED → GND |

IMU +X 朝枪口、+Y 朝左、+Z 朝上。刚性固定，避免扳机动作使基座形变。轴方向用实际运动验证；横滚补偿另行实现。

## 脚踏与结构

W/S/A/D 分别接 GPIO4/5/6/7，另一端 GND；LED 接 GPIO21。长线可从外部 10 kΩ 上拉、100 nF 对地滤波开始验证；RC 会增加延迟，不能替代软件去抖。

踏板使用独立承力和机械限位，微动仅负责检测。支持 W+A 等组合，四个独立 GPIO 无矩阵鬼键。验证回弹、连续踩踏与误触。

## USB 与引脚保留

- GPIO19/20：原生 USB D-/D+；USB-UART 桥不能枚举自定义 HID。
- GPIO26–32 与 Octal 存储相关 GPIO33–37：不分配给外设，实际引出以板卡为准。
- GPIO0/3/45/46：启动配置相关，不接普通操作按钮。
- GPIO39–42：预留给后续外部 JTAG，当前不接。
- 同时接两根 USB 或外部 5 V 前核对板卡电源隔离，避免回灌。

自制 PCB 还需 USB-C CC 下拉、ESD、供电保护和差分布线，不能直接照搬开发板排针接法。

HID 模式：原生 USB 提供游戏输入，UART 提供烧录/日志。JTAG 模式：原生 USB 提供内置 JTAG，固件不安装 TinyUSB；UART 仍可烧录/日志。若需 HID 与断点同时运行，应另行设计外部 JTAG/PHY；外部 JTAG 路由可能涉及 eFuse，不属于默认配置。

## 官方资料

- [TDK 数据手册](https://invensense.tdk.com/wp-content/uploads/2021/06/DS-000347-ICM-42688-P-v1.5.pdf)：引脚与接口。
- [乐鑫开发板指南](https://documentation.espressif.com/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)：USB 和存储器引脚。
# YD 板卡补充

用户购入的 YD-ESP32-S3 板卡接线、双 USB 和板载 RGB 差异见 [板卡核对](YD_ESP32_S3.md)。特别注意 GPIO21 是工程外接状态灯，板载 WS2812 在 GPIO48。


