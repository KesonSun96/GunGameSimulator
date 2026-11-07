# YD-ESP32-S3 板卡核对

依据用户提供的 `YD-ESP32-S3-SCH-V1.4.pdf`（2022-09-23）及 `S3-V1.2 (14).PNG`。资料包含不同版本与 N8R8/N16R8 示例，必须核对实际板子版本和模块丝印，不能用宣传图判定实物容量。

- 正面元件朝上、USB 口向下时，图片左口为原生 USB/OTG（GPIO19 D-、GPIO20 D+），右口经 CH343P 接 UART0。游戏 HID 和内置 JTAG 使用左口；烧录及日志优先右口。
- 原理图 BOOT 接 GPIO0，RST 接 EN；CH343P 的 RTS/DTR 有自动下载电路。自动下载失败时可按住 BOOT、点按 RST 后释放 BOOT。
- 原理图板载 RGB 是 GPIO48 驱动的 WS2812，需要数据协议；工程的 `PIN_LED_STATUS=21` 是外接普通 LED 输出，不会让板载 RGB 按状态闪烁。调试时使用 UART 日志，或给 GPIO21 接限流电阻与外置 LED。不要仅把宏改为 48 后用 gpio_set_level 驱动 RGB。
- 当前枪体 SPI GPIO10/11/12/13 和按键 GPIO4/5/6/7/15/16/17/18 都在排针引出，图中未显示板载冲突。GPIO19/20 留给 USB，GPIO0 留给 BOOT，GPIO43/44 留给 UART。
- 排针上能看到 GPIO35/36/37 不表示 N16R8 可以使用；本项目继续避开八线 PSRAM 占用引脚。实际 Flash 容量用模块丝印及 esptool flash-id 核对，N8 型号不能沿用 16 MB 构建假定。
- 原理图 USB VBUS 通过 D1/D2 进入 5V，排针 5Vin 另有 D3 与 IN-OUT 焊桥。焊桥状态会影响供电通路，接外部电源前核对实板，不能凭资料假设所有板修订都相同。IMU 和按键逻辑使用 3.3V。

这些资料可用于接线和机械尺寸核对。附带的 MicroPython 固件、Thonny 与旧下载工具不参与本项目 ESP-IDF 构建，无需安装或烧入。板卡实测仍需完成 USB 枚举、WHO_AM_I、逐轴符号、开关按下释放和 JTAG 断点验收。
