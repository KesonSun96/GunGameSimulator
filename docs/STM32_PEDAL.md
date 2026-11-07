# STM32 脚踏与 MCU 移植

当前默认脚踏目标为 STM32F103C8T6，工程位于 firmware/pedal。枪体继续使用 ESP32-S3。
旧 firmware/pedal-controller 是保留的 ESP32 脚踏参考后端，不是 STM32 构建入口。

## 分层规则

| 层 | 文件 | 职责 |
|---|---|---|
| 领域与应用 | components/gg_core、components/gg_app | 去抖、WASD、按键队列、会话与恢复策略 |
| 产品入口 | pedal/src/main.c、include/pedal_target.h | 选择 GG_DEVICE_PEDAL，注入 gg_hal_t |
| SDK/USB 适配 | pedal/src/ports/libopencm3/target.c | USB HID、完成事件、端点映射、调度、HAL 实现 |
| 板级 | pedal/src/boards/f103c8/board.c | 时钟、GPIO、时间、USB 外设与物理断开 |
| 构建选择 | pedal/platformio.ini、portable_sources.py | MCU、SDK、链接脚本、启动文件及源文件选择 |

上层禁止包含 SDK、寄存器、RTOS 和板级引脚。板级禁止实现 WASD 去抖或按键业务规则。
HAL 是项目的 gg_hal_t，不等同于 STM32Cube HAL。当前后端使用 libopencm3。
业务不依赖 libopencm3，其他厂家可使用自己的 SDK、USB 栈和构建工具。

换同系列板卡时新增 boards/<board>，实现 board.h 并增加构建环境，调整晶振、引脚、USB 上拉和下载方式。
换其他 STM32 系列时，先确认 SDK 和该芯片 USB device 支持，再实现 board.h 并选择对应 USB driver。
只有所选 SDK 的 USB API 和行为兼容时才能复用当前 ports/libopencm3。
换 Cube HAL、TinyUSB 或其他厂商 SDK 时新增 ports/<sdk>，实现 pedal_target_hal() 返回 gg_hal_t，
替换构建来源。保留 main.c、gg_app、gg_core，不向它们增加芯片条件编译。
芯片若没有 USB device 外设，需要外置 USB 控制器或另选与主机通信方案，不能只改芯片型号。

每个构建环境只编译一个目标适配和一个板级实现。构建必须使用真实 Flash/RAM 容量。
当前仅 f103c8 已实现并编译，不能据此宣称其他芯片已适配。

## F103C8 板卡假设与接线

采用常见 Blue Pill 接线，外部 HSE 8 MHz，系统 72 MHz，USB 48 MHz。
输入使用内部上拉，开关闭合接 GND，有效状态为低电平。
这不是对用户实物板卡晶振和接线的确认，烧录前应核对实物。

| 功能 | 引脚 |
|---|---|
| W / S / A / D | PB0 / PB1 / PB10 / PB11 |
| USB D- / D+ | PA11 / PA12 |
| USB D+ 上拉 | 外部 1.5 kΩ 至 3.3 V，按实际板卡检查 |
| 板载指示灯 | PC13，低电平亮 |
| ST-Link SWDIO / SWCLK | PA13 / PA14 |
| 调试共地 | GND |

BOOT0 保持低电平。USB 连接板上直连 MCU 的 USB 接口，不是 USB 转串口模块。
按板卡原理图选择供电，避免 USB 和调试器电源相互回灌。
当前断开实现复位 USB 外设后拉低 D+ 100 ms，再初始化并释放 D+。
若板卡使用可控上拉或其他 USB 电路，应替换 board_usb_attach。

## 工具链和操作

所有 STM32 工具位于工程同级的 .tools/stm32，与 .tools/esp32、.tools/w64devkit 分开。
在工程根目录的 PowerShell 中执行以下命令。

首次安装需要 Python 3.11 和网络，已有环境无需重复安装。

~~~powershell
python -m venv ../.tools/stm32/venv
../.tools/stm32/venv/Scripts/python.exe -m pip install platformio==6.1.18
./tools/pedal.ps1 build
~~~

platformio.ini 固定平台、libopencm3 和 ARM GCC 版本。首次构建自动安装依赖。
工具根目录可通过 -ToolRoot 指定，不在工程中保存本机绝对路径。

~~~powershell
./tools/pedal.ps1 doctor
./tools/pedal.ps1 build
./tools/pedal.ps1 test
./tests/run_host_tests.ps1
./tools/pedal.ps1 flash
~~~

flash 使用 ST-Link，必须先连接实物；本次未执行烧录。
产物为 firmware/pedal/.pio/build/f103c8/firmware.elf 和 firmware.bin。
VS Code 可以单独打开 firmware/pedal，使用 PlatformIO IDE 的 f103c8 环境进行构建和 ST-Link 调试。
首次调试可能下载 OpenOCD。断点会暂停 USB 服务，因此断点调试不能用于输入时延验收。
当前没有 UART 日志后端，可以通过调试器观察 main.c 中的 pedal.stats。

## USB 和调度契约

USB 接口号为 0，输入端点为 0x81，上层逻辑接口始终为 GG_HID_ITF_KEYBOARD（1）。
8 字节 boot keyboard 格式，无 Report ID，支持 GET/SET_PROTOCOL、GET/SET_IDLE、
输入 GET_REPORT 与 LED 输出 SET_REPORT。空闲重发不生成上层报告完成事件。
原型使用 pid.codes 的 1209:0001 私有测试标识，发布前需分配自己的身份。
参考 https://pid.codes/1209/0001/ 的适用范围。

前台循环轮询 USB，并以约 1 ms 周期调用应用，USB 中断不参与业务状态读写。
SysTick ISR 只更新毫秒计数。HAL 返回扩展后的 64 位微秒时间，但当前分辨率为 1 ms，
适合脚踏，不能直接作为枪体 IMU 微秒采样适配。
前台必须持续运行，不能阻塞超过 32 位毫秒计数的一次回绕周期。

submit_report 成功只代表复制并交给端点，真实完成回调才能发布 COMPLETE。
休眠保留旧在途 token，恢复分配新会话，旧完成仍携带旧会话。
配置撤销、总线复位和强制重连清理端点状态，禁止将旧确认归给新请求。
32 项事件队列溢出或传输超时触发物理重连，重连后的按键同步由上层统一处理。
没有独立 VBUS 检测，USB 拔线可能先表现为 suspend 或报告超时，重新枚举由总线复位处理。
不支持远程唤醒，不主动唤醒休眠电脑。

## 验证与交接

主机适配测试编译真实 target.c，使用真实 libopencm3 USB 声明，替换外设与时钟。
覆盖提交不等于完成、报告复制、端点忙、写入失败、会话隔离、休眠恢复、
未配置恢复保护、超时重连、描述符、空闲重发和事件溢出。
它不能替代 USB 控制器和主机上的实测。

交接给其他 AI 时必须保留以下验收项：
1. 原有主机用例通过，新后端完成事件必须来自实际完成。
2. 新目标实际交叉编译成功，记录 Flash 与静态 RAM，保留栈余量。
3. 上板验证单键和组合键、快速按放、长按、插拔、主机休眠恢复，不丢释放不卡键。
4. USB 抓包验证枚举、1 ms 端点轮询、HID 控制请求与重连。
5. 改硬件时更新接线、晶振、上拉、供电与下载说明，明确实测结果。

### 本次验证记录（2026-11-07）

- f103c8 实际交叉编译通过，构建日志无 warning/error。
- Flash 19716 / 65536 字节，静态 RAM 2812 / 20480 字节；静态统计不包含运行时栈峰值。
- STM32 USB 适配回归通过。
- 原有 core 3232、ESP32 USB 55、application HAL 38 项检查全部通过。
- 尚未烧录，尚未完成真实 USB 枚举、休眠恢复和按键验收。
