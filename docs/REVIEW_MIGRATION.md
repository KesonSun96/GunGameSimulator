# 迁移后的实现审查（2026-11-05）

后续 HAL 分层已完成：上层 `gg_app/gg_core` 无 SDK 依赖，驱动与 TinyUSB 已迁往 `firmware/platform/esp32`，文件位置以 [HAL_PORTING.md](HAL_PORTING.md) 为准。完整应用新增 38 项 HAL 测试并通过独立 CMake/CTest；下文保留迁移时审查记录。

纯 C 主机测试实际运行：3192 checks，0 failures。该测试不覆盖真实 TinyUSB 任务并发、传感器电气行为或 JTAG 驱动。

迁移验证：Doctor 成功识别 ESP-IDF v5.4.1；枪体/脚踏的 HID/JTAG 四种固件已在新路径重新构建成功（退出码均为 0）。OpenOCD 可运行并输出版本。构建日志位于 `D:/workspace/.tools/logs/migrated-*.log`。旧构建产物保留在各工程 `build-*.before-migration`，不要用于烧录或断点调试。没有执行烧录和板上断点验收。

## 重构结果

下列四项审查发现均已落实代码修复：

- RECENTER 按住始终进入/保持 CLUTCH，覆盖组合校准前等待、成功和失败返回；组合手势触发后必须两键都松开才能重新触发。
- 新增共享 `gg_usb`，用一个替代 USB 任务统一执行 TinyUSB 操作；通过命令/事件队列跨任务通信。纯 C `gg_usb_link` 保留提交时的 session/submit_id，挂起保留旧 token，重连及超时可恢复。
- 输入报告失败回调转发到核心失败处理。软件重连显式产生 DISCONNECT，事件溢出计数使用原子访问。
- 新增可离线测试的 IMU 解码器，拒绝全零及保留编码；驱动显式配置大端、关闭 HOLD_LAST_DATA 和 FSYNC tagging。格式依据 [TDK DS-000347 v1.7](https://invensense.tdk.com/wp-content/uploads/2022/12/DS-000347-ICM-42688-P-v1.7.pdf) 的 INTF_CONFIG0 描述。

额外修复：脚踏物理 HID 接口为 0，核心逻辑键盘为 1；原来的完成事件直接使用物理编号导致核心无法确认键盘报告，现在由共享传输层显式映射。

最终验证：核心/驱动解码回归 **3232 checks，0 failures**；运行生产 USB 传输代码的模拟 RTOS/USB 回归 **55 checks，0 failures**。覆盖旧会话排队命令拒绝、挂起后的旧完成事件、接口独立 token、输入/输出失败区分、底层拒绝、软件断开、队列溢出/背压。模拟测试不等同真实任务调度或 USB 抓包。

最终源码四种构建均成功，日志 `D:/workspace/.tools/logs/refactor-final-*.log`。未烧录或更改板卡 eFuse，真实枚举、采样、延迟与 F5 断点验收仍待执行。

## 原审查发现（修复前记录）

1. **P1：组合键校准等待期间仍可能输出运动。** `gg_gun_controller.c` 的 READY 分支只有 recenter && !profile 才进入 CLUTCH；同时按两键时在满 1 s 前仍执行 process_sample。校准成功分支也以 !profile 限制 CLUTCH。用户按组合键并摆正枪体时镜头会移动。RECENTER 按住应始终禁止运动，包括组合手势等待及校准返回阶段。
2. **P1：USB 完成事件没有保存提交时的会话身份。** 枪体 `usb_hid.c` 的完成回调使用当前 s_session 和可覆盖的 s_submit_id；这些字段跨应用任务/TinyUSB 任务无同步，提交也不校验 report.session。挂起/恢复或重枚举与提交交错时，完成事件可能归属错误或被忽略，造成超时重连。需要同步的在途 token，连接切换和提交的原子性，以及设备层事件交错测试；脚踏包装需同样检查。
3. **P2：实际 USB 层没有转发失败完成。** 核心已有 gg_hid_output_on_submit_failed，但真实包装只转发 COMPLETE；传输失败只能等待 100 ms 超时并重连，而非按规范及时重试。已核对本地 TinyUSB 的 hid_device.h/c，存在 tud_hid_report_failed_cb，失败分支实际调用该回调；两套固件应转发输入报告失败事件并接入核心失败接口。
4. **P2：IMU 无效编码尚未完整检查。** `icm42688_read_sample` 只拒绝全零，其他原始值全部标记 valid。应根据所选数据格式及数据手册检查无效轴编码，避免异常样本被用于瞄准和故障恢复判断。

## 板卡差异

YD 原理图的板载 LED 为 GPIO48 WS2812；当前 GPIO21 状态输出只适用于外接普通 LED，板载 RGB 不会按文档闪烁。详见 YD_ESP32_S3.md。资料覆盖多个 Flash 容量，必须确认实物 N16R8。

## 调试判据

源码已具备模块化调试入口，以上行为问题不妨碍在 JTAG 模式定位算法和按键问题。能否直接 F5 仍取决于迁移后完整构建、VS Code 扩展环境、USB JTAG WinUSB 驱动及真实开发板连接。未连接并实际停在 app_main 前，不标记板上调试通过。
