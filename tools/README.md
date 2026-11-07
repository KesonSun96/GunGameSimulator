# Windows 开发工具入口

> 默认脚踏已迁移至 STM32F103C8T6。当前目标的分层、接线、构建、测试和移植规则见 [STM32 脚踏指南](../docs/STM32_PEDAL.md)。下文 ESP32 脚踏内容仅适用于保留的旧后端。

主机测试脚本依次运行 `test_gg_core.exe`、`test_usb_transport.exe`、`test_app_hal.exe`。USB 测试以受控 RTOS/USB 替身运行生产传输代码；应用测试只依赖标准 C 上层和模拟 HAL，验证不同硬件映射及故障恢复。任一测试失败返回非零。也可从根 CMake 构建并执行三项 CTest，方法见 `docs/HAL_PORTING.md`。

`idf.ps1` 是 VS Code 任务和命令行共享入口，参数：

| 参数 | 可选值 |
|---|---|
| Project | gun-controller / pedal-controller |
| Action | doctor / build / flash / monitor / menuconfig |
| Mode | hid / jtag |
| Port | flash/monitor 必填，例如 COM5 |
| DryRun | 仅打印 idf.py 参数，不调用工具链、不访问设备 |

优先读取 tools/local.json 的 idfPath，否则使用 IDF_PATH。脚本导入 export.ps1，在 IDF Python 环境运行 idf.py；不安装软件，不自动下载整个 SDK，不修改全局执行策略。

每个模式使用独立 build-*、sdkconfig.*，defaults 采用公共加模式覆盖。脚本以参数数组启动程序，路径含空格也不拼接成可执行命令字符串。错误退出码传给 VS Code；缺失 SDK 时直接报告，不伪装编译成功。

```powershell
# 仓库根目录执行，无需 ESP-IDF 即可查看构建参数
.\tools\idf.ps1 -Project gun-controller -Mode jtag -Action build -DryRun
```

配置、驱动与完整操作见 [调试指南](../docs/DEBUGGING.md)。本地配置不得提交；安装 IDF 后的首次依赖锁文件应提交。

## 主机离线测试

`tests/run_host_tests.ps1` 不依赖 ESP-IDF 或开发板，用主机 gcc 编译 `gg_core`、`pedal_keys.c` 和测试文件：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\tests\run_host_tests.ps1
```

默认查找 PATH 中的 `gcc`，其次尝试 `..\.tools\w64devkit\w64devkit\bin\gcc.exe`；可用 `-Gcc <path>` 指定。构建产物在 `tests/build/`（已被 `.gitignore` 忽略），失败返回非零。用例清单见 [测试计划](../docs/TEST_PLAN.md)。
# 本机工具链布局

默认自动发现工程旁的 `../.tools/esp32/esp-idf`，并使用同目录的 `espressif` 工具及 `Python311`。`tools/local.json` 或 `IDF_PATH` 可覆盖 SDK 位置。主机 GCC 仍放在 `../.tools/w64devkit`；日志放 `.tools/logs`，安装包放 `.tools/downloads`。每套工具链独立目录，未来其他平台不要混入 esp32。VS Code ESP-IDF 扩展仍需在其环境设置中选同一 SDK 和 tools 路径。


