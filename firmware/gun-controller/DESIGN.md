# 枪体模块设计

> 当前描述对应已按 [V1 架构](../../docs/ARCHITECTURE.md) 重构的固件。行为判据以架构文档为准，实施进度见 [实施计划](../../docs/IMPLEMENTATION_PLAN.md)。

## 模块边界

输入为三轴角速度/加速度和 8 个按键，输出鼠标 X/Y、左右键、R/1/2/左 Ctrl。固件不知道游戏内部镜头角度或开镜状态。

| 文件 | 职责 |
|---|---|
| `firmware/components/gg_core/gg_debounce.c` | 时间注入式去抖，枪体与脚踏共用 |
| `gg_core/gg_calibration.c` | 2 s 窗口统计、失败原因、原子提交 bias |
| `gg_core/gg_aim.c` | 去 bias、固定 Y/Z 映射、55 Hz LPF、软死区、线性增益、dt 积分与余量 |
| `gg_core/gg_hid_output.c` | 快照 FIFO、运动积累、在途/完成、溢出与断线恢复 |
| `gg_core/gg_gun_controller.c` | 状态机、clutch、组合手势、校准调度、IMU 故障 |
| `main/main.c` | 仅选择枪体产品并调用平台启动器 |
| `gg_app/src/gg_app.c` | 可移植周期、去抖、事件分发、恢复调度、指示与日志策略 |
| `platform/esp32/common` | 时钟、任务调度、GPIO、结构化日志后端 |
| `platform/esp32/gun/board.c` | 注入枪体 HAL 函数表 |
| `platform/esp32/gun/icm42688.c` | SPI、ID 检查、新样本、时间戳、非阻塞恢复 |
| `platform/esp32/gun/usb_hid.c` | 复合 HID 描述符、共享传输适配 |
| `platform/esp32/components/gg_usb` | 唯一 USB 任务、命令/事件队列、连接和完成/失败回调 |
| `gg_core/src/gg_usb_link.c` | 提交时会话 token、在途保护、挂起与超时逻辑 |
| `platform/esp32/gun/icm42688_decode.h` | 原始大端样本解码、保留编码和全零数据拒绝 |
| `platform/esp32/gun/usb_hid_stub.c` | JTAG 构建禁用 HID，保留内部 USB PHY |
| `platform/esp32/gun/board_config.h` | 引脚、USB 标识、IMU 量程/ODR；产品增益在 gg_app |

纯 C 模块不访问 GPIO、USB、日志，可在主机用受控时钟和模拟事件测试。

## 核心接口

| 模块 | 接口 | 约定 |
|---|---|---|
| 去抖 | `gg_button_init/update/pressed/just_pressed/just_released/held_us` | 低有效：`stable=false` 表示按下；边沿只在单次 update 内有效 |
| 校准 | `gg_calibration_start/feed/poll/reason/reason_str` | 只接受 NEW 且 valid 的样本；2 s 到点评估；只有全部检查通过才写 `out_bias` |
| 瞄准 | `gg_aim_init/set_bias/set_profile_gain/set_ads/reset_motion/update` | `update` 返回 false 表示仅初始化或 dt<=0，此时不产生位移；输出为 int16 counts |
| HID | `gg_hid_output_init/set_mouse_buttons/set_keyboard/add_motion/clear_motion/tick/on_connect/on_disconnect/on_suspend/on_resume/on_submit_complete/on_submit_failed` | `on_*` 由 USB 事件驱动；`tick` 处理超时、恢复和提交，每 1 ms 调用 |
| 控制器 | `gg_gun_controller_init/start/tick/recovery_due/on_recovery_result/on_usb_change` | `tick` 每 1 ms 调用；USB 连接状态变化时用 `on_usb_change` 清滤波和余量 |

接口契约以 `firmware/components/gg_core/include/*.h` 的注释为准，设备层不得绕过这些接口直接修改变量。

## 默认参数

| 项 | 值 |
|---|---|
| USB | VID/PID `0x303A/0x8266`，产品名 `GunGame Gun Controller`，序列 `GG-GUN-0001` |
| IMU | SPI2，SCK/MOSI/MISO/CS = 12/11/13/10，10 MHz，mode 0 |
| 量程/ODR | ±2000 dps、±8 g、1 kHz（`IMU_ODR_CODE=0x06`） |
| 按键 | 触发/ADS/R/1/2/蹲伏/Profile/Recenter = 4/5/6/7/15/16/17/18，低有效 |
| 去抖 | 扳机 1.5 ms、ADS 3 ms、其余 5 ms；LED = GPIO21 |
| 任务 | core 1、优先级 12、栈 4096 字节、周期 1 ms |
| 瞄准 | 死区 0、3 counts/°、档位 0.6/1.0/1.2、ADS 倍率 1 |
| 主机键 | 鼠标左/右键 = 扳机/ADS；键盘 R/1/2/左 Ctrl = 0x15/0x1E/0x1F/0x01 |

## 执行架构

`app_main → gg_esp_platform_start(GG_DEVICE_GUN) → gg_esp_board_hal → gg_app_init → gg_app_run`。应用只调用 HAL；ESP32 启动器建立 core 1、优先级 12、栈 4096 的应用任务，TinyUSB 回调通过事件队列交给应用。底层类型与句柄不跨 HAL，具体替换方式见 [HAL 移植指南](../../docs/HAL_PORTING.md)。

周期顺序：USB 事件 → 按键扫描 → 控制器（镜像按键、手势、新样本判定、状态机）→ HID tick → 服务重连 → LED → 统计。调度超过 2 ms 时只累计超期计数并重设唤醒基准，不追补多个伪周期。IMU 采用 1 kHz ODR，只有 `INT_STATUS.DATA_RDY` 置位时才读取整帧并生成带时间戳的新样本。

## IMU 驱动

- ESP32 适配使用 SPI2 mode 0、10 MHz，GPIO10 由 SPI 驱动控制 CS，单次读事务上限 16 字节。上电读 `WHO_AM_I(0x75)=0x47`；写 `DEVICE_CONFIG(0x11)=0x01` 软复位，10 ms 后复查 ID。
- 配置顺序：`PWR_MGMT0(0x4E)=0x00` → 1 ms → `GYRO_CONFIG0(0x4F)`/`ACCEL_CONFIG0(0x50)` 写入 ±2000 dps、±8 g、ODR code `0x06`（1 kHz）→ `GYRO_CONFIG1(0x51)=0`、`GYRO_ACCEL_CONFIG0(0x52)=0` → `PWR_MGMT0=0x0F` → 50 ms 启动等待。
- 每周期读 `INT_STATUS(0x2D)` bit3 `DATA_RDY`（读清）。无新数据返回 `GG_SAMPLE_NONE`，不推进滤波、校准或积分。
- 有数据时从 `ACCEL_DATA_X1(0x1F)` 连续读 12 字节（加速度 6 + 角速度 6），大端 int16 按 16.4 LSB/dps、4096 LSB/g 换算。时间戳取观察到就绪的单调时间，序号自增；12 字节全零判 `GG_SAMPLE_INVALID`，SPI 失败返回 `GG_SAMPLE_IO_ERROR`。
- 恢复状态机非阻塞：`RESET_WAIT` 10 ms → `OFF_WAIT` 1 ms → `STARTUP_WAIT` 50 ms；任一步通信失败立即 `FAILED`。故障期间控制器每 1 s 请求一次，上一步未完成前不重复启动。

## 状态机与交互

`BOOT → CALIBRATING → READY`；校准失败且无旧 bias 时进入 `NEEDS_CALIBRATION`，连续 20 ms 无有效新样本进入 `IMU_FAULT`。

| 事件 | 实现 |
|---|---|
| RECENTER 单独按住 | READY → CLUTCH，立即清除滤波、余量和未提交运动；松开清基准后回 READY |
| RECENTER+PROFILE 同按 1 s | 触发一次窗口校准；两键释放前不重复触发 |
| PROFILE 单键释放 | 按 0.6/1.0/1.2 顺序循环切档；组合手势消费本次 PROFILE |
| 校准成功 | 原子写入三轴 bias，清运动；RECENTER 仍按住则进 CLUTCH |
| 校准失败 | 有旧 bias 且 RECENTER 按住回 CLUTCH，否则有旧 bias 回 READY；无 bias 进 NEEDS_CALIBRATION |
| IMU 故障 | 每 1 s 请求一次非阻塞重初始化；成功前禁止运动，按键始终可用 |
| USB 状态变化 | HID 结束会话并恢复一致状态；控制器清滤波与余量，不重播离线动作 |

扳机/ADS 输出鼠标左右键，R/1/2/左 Ctrl 输出键盘；PROFILE/RECENTER 不输出主机键。

## 校准与瞄准契约

- 窗口 2 s、至少 1800 个有效新样本、最大间隔 5 ms；均值 ≤3 dps、标准差 ≤0.3 dps，加速度模长 0.9..1.1 g、标准差 ≤0.02 g。失败原因逐项返回，不写入部分结果。统计使用 Welford 在线均值/方差，不保存 2 s 原始样本。
- 默认不做自动零偏跟踪，也不提供单样本重置；手动校准只通过组合手势触发。
- 运动链按真实 dt 计算；`dt <= 0` 或 `dt > 5 ms` 丢弃该段并重建基准，不清零后伪造时间。
- 首次样本只初始化滤波，不输出。小数余量跨帧保留，clutch、校准提交、USB 状态变化、故障和异常 dt 均清零。
- 默认死区 0，`counts_per_degree=3`，三档增益 0.6/1.0/1.2，ADS 倍率 1。轴映射固定 `yaw_right=-gz`、`pitch_up=-gy`、`dy=-pitch`。
- V1 不补偿横滚倾斜，基本水平握姿是已知限制。

## HID 契约

鼠标报告 5 字节 `[buttons, dx, dy, wheel, pan]`，键盘 8 字节 `[modifier, reserved, key0..key5]`，端点 `bInterval=1 ms`。每接口 32 项完整快照 FIFO，每接口最多一个在途报告；只有完成事件才移除队头。完成/失败事件携带会话号和提交标识，旧会话的事件被忽略。运动宽整数积累，每轴 ±256、拆包 ±127，提交成功后扣除，在途失败不重发。

队列溢出清空未提交内容并等待在途完成后先发全释放、再发最新快照；在途 100 ms 未完成则结束会话并请求 USB 重连。断线/挂起立即清队列和运动，重连先全释放再同步当前状态。USB 事件队列（32 项）溢出时计入统计，同样按断线处理并请求重连，避免丢失完成通知后永久卡键。

LED 按单调时间闪烁：IMU 故障 100 ms、NEEDS_CALIBRATION 250 ms、CALIBRATING 500 ms、其他状态未连接 1 s，连接后常亮。热路径不动态分配、不写文件、不逐样本打印。

## 调试观察

JTAG 断点：`gg_app_tick`、`icm42688_read_sample`、`gg_gun_controller_tick`、`gg_calibration_poll`。平台启动器的 `s_app` 保存 controller、hid、stats 和去抖状态。JTAG 桩始终未连接，不验证 USB。

HID 构建的 UART 日志每 5 s 输出 state/cal/usb/prof/valid/invalid/io/maxdt/overrun/exec/q/ovf/drop/done/recon。`exec` 是循环执行耗时，不是调度间隔。

参见 [调试指南](../../docs/DEBUGGING.md)、[测试计划](../../docs/TEST_PLAN.md)。
