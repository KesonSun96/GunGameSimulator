# GunGameSimulator

基于 **ESP32-S3 N16R8 + ICM-42688-P** 的有线 FPS 游戏控制器。枪体作为 USB HID 鼠标和键盘，独立四向脚踏作为 USB HID 键盘。使用相对角速度瞄准，仍需零偏校准；不是绝对定位光枪。

## 默认硬件与脚踏入口

枪体为 ESP32-S3，脚踏现为 STM32F103C8T6。STM32 构建、接线、ST-Link 调试及更换 MCU 的规则见 [STM32 脚踏指南](docs/STM32_PEDAL.md)。运行 `./tools/pedal.ps1 build` 构建脚踏。以下 ESP-IDF 步骤用于枪体和保留的 ESP32 脚踏后端。

## 开始开发

1. 在 VS Code 安装 Espressif ESP-IDF 和 Microsoft C/C++ 扩展，安装并配置 ESP32-S3 工具链。
2. 打开 `GunGameSimulator.code-workspace`；首次完整调试建议直接打开 `firmware/gun-controller` 文件夹。
3. 将 `tools/local.example.json` 复制为 `tools/local.json`，填写本机 `idfPath`（也可使用已经配置的 `IDF_PATH`）。
4. 运行 **终端 → 运行任务 → GG: Doctor**，随后运行 `GG: Build HID`、`GG: Flash HID` 和 `GG: Monitor HID`。烧录时输入开发板 UART 口对应的 COM 编号。
5. 单步调试按 [VS Code 调试指南](docs/DEBUGGING.md) 烧录无 HID 的 JTAG 构建，再按 F5。

正常使用：原生 USB 接游戏电脑，UART 口用于烧录和日志。内置 USB JTAG 与 HID 共用内部 PHY，因此调试构建关闭 HID；它不能用于游戏输入验收。

## 项目结构

当前分层：`firmware/components/gg_app`（可移植应用）→ `gg_hal_t` → `firmware/platform/esp32`（平台适配和驱动），算法与队列在 `gg_core`。更换平台、传感器、接线或 USB 栈时只需实现对应 HAL 和目标启动/构建，详见 [HAL 移植指南](docs/HAL_PORTING.md)。根目录 CMake 可独立构建纯 C 库与主机测试，不需要 ESP-IDF；ESP32 目标使用 ESP-IDF CMake，STM32 脚踏使用独立 PlatformIO 构建。

实现入口：[V1 架构与规则](docs/ARCHITECTURE.md) → [AI 实施计划与验收](docs/IMPLEMENTATION_PLAN.md)。这是行为判据；当前代码已按该规范重构，但硬件验收尚未执行。

| 路径 | 用途 |
|---|---|
| [Guidance.md](Guidance.md) | 总体方案、实现差距、里程碑 |
| [firmware/gun-controller](firmware/gun-controller/DESIGN.md) | 枪体工程与模块设计 |
| [firmware/pedal](docs/STM32_PEDAL.md) | 默认 STM32 脚踏、板级与 SDK 适配 |
| [firmware/pedal-controller](firmware/pedal-controller/DESIGN.md) | 保留的 ESP32 脚踏参考后端 |
| [docs/HARDWARE.md](docs/HARDWARE.md) | BOM、引脚、供电 |
| [docs/DEBUGGING.md](docs/DEBUGGING.md) | VS Code、UART、JTAG |
| [docs/TEST_PLAN.md](docs/TEST_PLAN.md) | 实机验收与记录 |
| [tests](tests/test_gg_core.c) | 纯 C 主机离线测试与运行脚本 |
| [tools](tools/README.md) | Windows 构建任务入口 |

## 当前状态

代码已按 V1 架构拆分为共享 `gg_core`（去抖、窗口校准、线性瞄准、HID 输出、枪体状态机）和各自工程设备层：

ESP32 后端 USB 传输由共享 `gg_usb` 单任务执行，枪体/脚踏共用命令与事件队列、会话 token 和完成/失败处理。RECENTER 在组合校准等待及校准返回时也保持暂停运动。重构细节与验证记录见 [迁移审查](docs/REVIEW_MIGRATION.md)。

- 枪体：1 kHz 任务、DRDY 新样本与时间戳、非阻塞 IMU 恢复、状态机、clutch、RECENTER+PROFILE 手动校准、复合 HID、统计日志。
- 脚踏：复用快照 FIFO、完成确认、溢出/断线恢复和 W/S/A/D 组合输出。
- 测试：`tests/run_host_tests.ps1` 驱动真实纯 C 模块，覆盖实施计划中的校准、瞄准、clutch、手势、USB 忙/失败/溢出/超时/重连、IMU 故障和脚踏组合用例。

待完成：真实板卡上的枚举、轴符号、采样间隔、延迟、校准和按键验收；counts/° 与死区标定；NVS 参数保存；横滚补偿属于 V1 之后的增强项。文档中的已实现描述不等于板上验收通过。

