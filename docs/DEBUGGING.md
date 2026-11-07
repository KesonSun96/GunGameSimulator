# VS Code 构建与调试

> 默认脚踏已迁移至 STM32F103C8T6。当前目标的分层、接线、构建、测试和移植规则见 [STM32 脚踏指南](STM32_PEDAL.md)。下文 ESP32 脚踏内容仅适用于保留的旧后端。

迁移到 `D:/workspace/GunGameSimulator` 后，脚本自动发现 `D:/workspace/.tools/esp32/esp-idf`，工具目录为 `D:/workspace/.tools/esp32/espressif`，Python 为同目录 `Python311`。这只配置脚本进程；VS Code 的 ESP-IDF 扩展需选择同一套 SDK 和工具目录，再运行 Doctor。机器上的旧 `IDF_PATH` 或 `tools/local.json` 会优先于自动发现，须去掉或改正旧值。迁移前的构建目录已备份为 `build-*.before-migration`，调试只使用重新生成的 `build-jtag`。

YD 板卡的 USB 口和 LED 差异见 [板卡核对](YD_ESP32_S3.md)，实现风险见 [迁移审查](REVIEW_MIGRATION.md)。GPIO21 状态灯需要外接，不要以板载 RGB 是否闪烁判断程序是否运行。

## 1. 环境准备

使用 Windows PowerShell、VS Code、Espressif ESP-IDF 扩展和 Microsoft C/C++ 扩展。使用支持 `gdbtarget` 的新版 ESP-IDF 扩展；配置参考官方当前调试接口。若旧扩展提示未知 debug type，先升级扩展，不要混用旧版 espidf launch 配置。

通过 Espressif 安装工具/扩展完成 ESP-IDF、Python 虚拟环境、Xtensa 编译器、CMake、Ninja、OpenOCD 和 USB 驱动安装。建议先选 ESP-IDF 5.4.x 作为本项目验证基线；当前已在 ESP-IDF 5.4.1 + `esp_tinyusb` 1.7.6 + TinyUSB 0.21.0 下完成四种组合构建。升级时记录实际版本并重跑验收，依赖解析成功后提交 dependencies.lock。

用扩展的环境配置界面选择所装 IDF。再复制 `tools/local.example.json` 为 `tools/local.json`，将 idfPath 改为真实目录。示例路径不是自动安装位置。脚本配置与扩展必须选同一套 IDF。

脚本在当前 PowerShell 进程加载 export.ps1；不要从 PowerShell 直接运行 export.bat 并假定其环境会传回来。`tools/local.json` 和机器上的扩展安装路径不进入 Git。

## 2. 打开方式

- `GunGameSimulator.code-workspace` 同时显示 Overview、Gun 和 Pedal。ESP-IDF 扩展选择 Gun 或 Pedal；Overview 的根 CMake 是主机/可移植库工程，不是 ESP-IDF 固件工程。
- 第一次调试最稳妥的方式：VS Code 直接打开 `firmware/gun-controller` 或 `firmware/pedal-controller` 文件夹，避免多工程上下文混淆。
- 每个工程有独立 `.vscode/tasks.json`、`launch.json`、`settings.json`。构建任务名称相同，多根工作区选择对应文件夹的任务。
- `.vscode` 配置入库；扩展生成的机器绝对路径放用户设置，避免提交本机路径。

## 3. 两种构建模式

| 模式 | 输出 / 配置 | USB | 用途 |
|---|---|---|---|
| HID | build-hid / sdkconfig.hid | TinyUSB HID | 游戏输入、延迟、枚举测试；UART 日志 |
| JTAG | build-jtag / sdkconfig.jtag | 内置 USB Serial/JTAG | 断点、单步、变量；无 HID 游戏输入 |

默认参数分别来自 sdkconfig.defaults 与 sdkconfig.defaults.hid/jtag。`main/Kconfig.projbuild` 定义 `CONFIG_GG_DEBUG_NO_USB_HID`；JTAG 将其设为 y，由 CMake 选择 `usb_hid_stub.c`，完全不安装 TinyUSB 驱动；同时选择调试优化。HID 模式保持普通优化和真实 USB HID。

HID 使用共享 `gg_usb` 的一个替代 USB 任务；必须配置 `CONFIG_TINYUSB_NO_DEFAULT_TASK=y`、关闭 `CONFIG_TINYUSB_INIT_IN_DEFAULT_TASK`，否则编译会明确报错，避免同时启动两个 TinyUSB 任务。新 defaults 已设置前者；旧 sdkconfig 需同步核对。JTAG 不启动共享 USB 任务。

原生 USB OTG 与 USB Serial/JTAG 共用内部 PHY，不能假定普通开发板能同时使用 HID 和内置 JTAG。默认框架不修改 eFuse。若未来需同时调试真实 HID，应单独规划外部 JTAG/PHY 及相应硬件配置。

## 4. 正常 HID 联调

1. 原生 USB 连接 PC，UART 桥连接 PC；确认板卡供电允许这样连接。
2. 运行任务 `GG: Doctor` 检查 IDF 路径、可见串口和 idf.py 版本。
3. `GG: Build HID`（Ctrl+Shift+B 默认构建 HID）。首次需要联网解析依赖。
4. `GG: Flash HID`，输入 UART 桥的 COM 编号。flash 自带构建依赖。
5. `GG: Monitor HID` 使用同一 COM；IDF Monitor 通常用 Ctrl+] 退出。
6. 枪体静置完成校准后检查输入；脚踏先在文本编辑器观察组合与释放。

不要把 COM 编号写死，两个板的 UART 口分别确认。监视器占用串口时先退出再烧录。烧录失败先核对线材、UART 口及 BOOT/RESET 操作；原生 USB 正在运行 HID 时不提供普通串口。

## 5. VS Code 单步调试

1. 连接 UART 和原生 USB，安装 Espressif 的 USB JTAG WinUSB 驱动。
2. 退出所有占用 UART 的监视器，停止其他 OpenOCD 会话；一次调试一块板。
3. 在对应工程选择 `GG: Built-in JTAG (HID disabled)`，按 F5。
4. preLaunchTask 自动执行 `GG: Flash JTAG`，输入 UART 的 COM 编号，将关闭 HID 的固件烧入。
5. 烧录后等待原生 USB 重新枚举，再由扩展启动 OpenOCD 和 GDB，目标为 app_main。若枚举尚未完成导致连接失败，稍候重试。
6. F9 设置断点，F10 单步，F11 进入函数，Shift+F5 结束调试。需要日志可在烧录完成后另开 `GG: Monitor JTAG`。
7. 回到游戏测试前，停止调试/OpenOCD，执行 `GG: Flash HID` 恢复正常固件。

JTAG 模式 mounted=false、LED 闪烁、完成后 `done=0` 是预期。它用于验证 IMU、算法和 GPIO，不能测 HID、端到端延迟或断点暂停期间的实时行为。

launch 指向本工程 build-jtag 下的 ELF。扩展 idf.buildPath 也指向 build-jtag；源码补全使用其中的 compile_commands.json，首次需先 Build JTAG。不要用旧 build/ 的 ELF 调试新固件。提供的任务负责配置隔离；不建议混用扩展默认 Build/Flash 按钮，避免生成另一份 sdkconfig。

## 6. 命令行等效入口

在仓库根目录的 PowerShell 中：

```powershell
.\tools\idf.ps1 -Project gun-controller -Action doctor
.\tools\idf.ps1 -Project gun-controller -Mode hid -Action build
.\tools\idf.ps1 -Project gun-controller -Mode hid -Action flash -Port COM5
.\tools\idf.ps1 -Project gun-controller -Mode hid -Action monitor -Port COM5
.\tools\idf.ps1 -Project pedal-controller -Mode jtag -Action build
```

COM5 只是示例。系统阻止运行本地脚本时，可使用 `powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\idf.ps1 ...`，仅影响当前进程；VS Code 任务已这样设置。

已有 sdkconfig 会保留菜单配置，修改 defaults 不会强制覆盖旧值。需要重置时先备份对应 sdkconfig.hid/jtag，再移走该模式的配置与 build 目录重新构建；不要让 HID 模式打开 GG_DEBUG_NO_USB_HID。首次 JTAG 构建检查 sdkconfig.jtag 中该项为 y。

## 6.1 主机离线测试

无需 ESP-IDF 或开发板，在仓库根目录：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_host_tests.ps1
# 指定编译器
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_host_tests.ps1 -Gcc C:\path\to\gcc.exe
```

脚本默认使用 PATH 中的 gcc，其次尝试 `..\.tools\w64devkit\w64devkit\bin\gcc.exe`；以 `-Wall -Wextra -Werror` 编译 `gg_core`、`pedal_keys.c` 和测试文件到 `tests/build/`，任一检查失败返回非零。用例覆盖见 [测试计划](TEST_PLAN.md)。

## 7. 常见问题

| 现象 | 排查 |
|---|---|
| Doctor 找不到 IDF | 完成安装，填写 local.json 或 IDF_PATH；example 不是安装器 |
| 找不到 gdbtarget | 更新 ESP-IDF 扩展并完成其环境配置 |
| OpenOCD 找不到设备 | 原生 USB 数据线、JTAG 驱动、是否已烧 JTAG 固件、是否有另一会话 |
| HID 消失 | 检查是否还在 JTAG 模式；烧回 HID |
| 找不到 ELF / 补全错误 | Build JTAG，确认当前工程和 build-jtag 路径 |
| 变量 optimized out | 确认调试优化；必要时在变量有效作用域断点 |
| IMU ID 不对 | 检查 SDO/AD0 不接地、供电、CS、SPI 线、模块接口能力 |

## 8. 验证边界与来源

本机已完成 ESP-IDF 5.4.1 下枪体/脚踏的 HID/JTAG 四种组合构建，以及 `tests/run_host_tests.ps1` 的主机离线测试；尚未完成真实板卡的 USB 枚举、断点、采样间隔和端到端延迟验收。先执行测试计划 M0/M1，不应把编译成功当作硬件验收。

- [VS Code 调试配置官方说明](https://docs.espressif.com/projects/vscode-esp-idf-extension/en/latest/debugproject.html)
- [ESP32-S3 USB PHY 说明](https://docs.espressif.com/projects/esp-iot-solution/en/latest/usb/usb_overview/usb_phy.html)
- [外部 JTAG 配置](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-guides/jtag-debugging/configure-other-jtag.html)

