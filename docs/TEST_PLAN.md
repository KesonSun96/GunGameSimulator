# 验收与测试记录

> 默认脚踏已迁移至 STM32F103C8T6。当前目标的分层、接线、构建、测试和移植规则见 [STM32 脚踏指南](STM32_PEDAL.md)。下文 ESP32 脚踏内容仅适用于保留的旧后端。

HAL 分层验证：完整 `gg_app` 仅用标准 C 头文件编译，38 项检查全部通过；既有核心 3232 项和 USB 传输 55 项仍全部通过。根目录 CMake 生成两个可移植库，三项 CTest 全部通过。覆盖两种不同接线的 HAL 产生相同报告、实例隔离、缺失/错误接口拒绝、初始按住、输入失败释放且不切档、无效浮点拒绝、IMU 初始化失败/恢复、日志与超期统计。ESP32 底层文件已迁移至 `firmware/platform/esp32`，最终四组合构建日志为 `.tools/logs/hal-final-*.log`。实机测试未执行。

最新重构验证（2026-11-05）：核心/IMU 解码 3232 项检查、USB 传输 55 项检查，均零失败；两设备 HID/JTAG 四种最终构建全部成功。USB 传输用受控 RTOS/USB 替身执行生产代码，覆盖脚踏物理/逻辑接口转换及会话交错。详细变更及日志位置见 [重构审查记录](REVIEW_MIGRATION.md)。下面保留首次实现的历史结果；所有板上测试仍待执行。

V1 以 [ARCHITECTURE.md](ARCHITECTURE.md) 为行为判据，必须补齐 [IMPLEMENTATION_PLAN.md](IMPLEMENTATION_PLAN.md) 中的离线用例及四种构建，再执行本文的板上验收。手动校准操作统一为 RECENTER+PROFILE 同时按住 1 s；RECENTER 单独使用为 clutch。旧原型不应被假定已符合这些规则。

硬件结果初始为“未执行”；下方离线记录只代表纯逻辑与编译验证。建议每次硬件记录放在 docs/test-results/YYYY-MM-DD.md，包含固件提交/工作区差异、IDF/组件版本、板卡与 IMU 型号、模式、参数、游戏设置、环境温度、日志与结果。

## 离线与构建记录（2026-11-05）

已执行，仅覆盖纯逻辑与编译，不代表板上通过：

| 项目 | 命令 | 结果 |
|---|---|---|
| 主机离线测试 | `tests/run_host_tests.ps1`（w64devkit gcc 16.2.0，`-Wall -Wextra -Werror`） | 3192 checks，0 failures |
| 枪体 HID | `idf.py -C firmware/gun-controller -B build-hid -D SDKCONFIG=sdkconfig.hid ... build` | 成功，`gun_controller.bin 0x40fb0` |
| 枪体 JTAG | 同上，`build-jtag`/`sdkconfig.jtag` | 成功，`0x3d3b0`，`CONFIG_GG_DEBUG_NO_USB_HID=y` |
| 脚踏 HID | `idf.py -C firmware/pedal-controller -B build-hid -D SDKCONFIG=sdkconfig.hid ... build` | 成功，`pedal_controller.bin 0x38670` |
| 脚踏 JTAG | 同上，`build-jtag`/`sdkconfig.jtag` | 成功，`0x343e0`，`CONFIG_GG_DEBUG_NO_USB_HID=y` |

离线用例覆盖：校准成功/缺样/间隔/均值/方差/加速度、失败保留旧 bias、首次失败禁止运动；低速输入、恒速与非均匀 dt 积分、零/非法 dt、死区；clutch、组合手势与单键长按；USB 忙按键顺序、提交失败重试、运动不重发、FIFO 溢出恢复、在途超时与旧会话隔离、断线/挂起不回放；IMU 20 ms 故障与恢复后重新校准；脚踏 W+S、A+D、W+A 与全释放。IDF 版本 5.4.1，`esp_tinyusb` 1.7.6，TinyUSB 0.21.0。

M1–M3 的枚举、轴符号、采样间隔、端到端延迟、按键/IMU 故障注入和游戏内标定仍未执行。

## M0 工具链和调试

- 两工程各构建 HID/JTAG，共四种组合；保存实际命令与退出状态。
- 核对 JTAG sdkconfig 中 GG_DEBUG_NO_USB_HID=y，HID 中关闭；检查输出 ELF 分别位于对应模式目录。
- UART 烧录/监视正常；JTAG 能停在 app_main，观察一个按键变量。
- 烧回 HID 后枚举正确。静态 JSON/脚本检查不能替代上述验收。

## M1 硬件与输入

- IMU WHO_AM_I=0x47，静止加速度模长约 1g；逐轴转动验证符号。
- 每个按钮按下、保持、释放；检查无反复触发和卡键。
- 脚踏 W/A/S/D、W+A、W+D，及 W+S 的约定行为。
- 启动已按住、长按后拔 USB、断线期间松开、重新连接。

## M2 瞄准和校准

- 静置 10 min，记录累计 counts、温度与 bias，不仅目测光标。
- 缓慢连续转动（含低于 1°/s）、同角度不同速度、往返重复动作，记录漏响应和非线性误差。
- 校准中移动、SPI 读失败、有效样本不足，失败后旧 bias 不变。
- clutch 按住搬动、松开，准星无旧位移回放；此项在功能实现后执行。
- 水平与横滚 ±30° 握姿比较串扰，区分当前限制与后续补偿效果。
- ADS hold 模式，固定游戏灵敏度，分别记录有/无设备端增益。

## M3 调度、USB 与故障

- 连续运动下测主机报告间隔、成功发送数、超期、积压/丢弃数；静止不发重复报告不算掉帧。
- 用 GPIO/逻辑分析或主机捕获建立时间关联；分别报告扳机和运动延迟的 P50/P95/P99/最大值。不得用带断点数据衡量实时性。
- USB 忙、重连与 IMU 故障时验证无持续卡键、无旧运动回放、错误可诊断。
- 先记录基线再冻结定量阈值；原方案 2–5 ms 和 <0.05° 不是已通过指标。

## 记录模板

| 测试 | 参数/步骤 | 预期 | 实测 | 结论/问题 |
|---|---|---|---|---|
| 示例：clutch | 按住转动再松开 | 不移动、不回放 | 主机离线已通过；实机未执行 | 待实机确认 |

退出条件：M0/M1 通过后才能开展整机体验；V1 发布前完成 M2/M3、脚踏机械验收和至少一局游戏记录。失败项回到 Guidance 里程碑跟踪，不用大死区掩盖漂移问题。

