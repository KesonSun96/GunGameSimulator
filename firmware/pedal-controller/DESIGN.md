# 脚踏模块设计

> 当前描述对应已按 [V1 架构](../../docs/ARCHITECTURE.md) 重构的固件：脚踏复用 `gg_core` 的快照 FIFO、完成确认、溢出与断线恢复逻辑。

## 职责与架构

四个低有效开关输出 W/S/A/D，支持组合方向（W+A、W+S、A+D 均同时输出，由游戏解释）。独立 USB HID 键盘，不依赖枪体。目标板为 ESP32-S3 N16R8。

| 文件 | 职责 |
|---|---|
| `gg_app/src/gg_app.c` | 纯 C 去抖、完整快照、事件、指示与日志策略 |
| `gg_app/src/pedal_keys.c` | 纯 C 映射：逻辑方向 → 6 键报告体 |
| `main/main.c` | 仅选择脚踏产品并调用平台启动器 |
| `platform/esp32/pedal/board.c` | 逻辑输入与传输 HAL，不需要 IMU |
| `platform/esp32/pedal/usb_hid_kbd.c` | 键盘描述符、共享传输适配 |
| `platform/esp32/components/gg_usb` | USB 单任务、物理接口 0 到逻辑键盘 1 的映射 |
| `platform/esp32/pedal/usb_hid_stub.c` | JTAG 构建禁用 HID |
| `platform/esp32/pedal/board_config.h` | 引脚、USB 标识；去抖和键值属于可移植上层 |
| `firmware/components/gg_core/gg_hid_output.c` | 每接口快照 FIFO、在途报告、溢出/断线恢复 |

`app_main → gg_esp_platform_start(GG_DEVICE_PEDAL) → gg_esp_board_hal → gg_app_init → gg_app_run`。应用仅依赖 HAL；ESP32 后端建立 core 1、优先级 12 的 1 ms 任务，调度超期后重设唤醒基准。移植方式见 [HAL 指南](../../docs/HAL_PORTING.md)。

每个周期从当前踏板状态构造 6 键完整快照，交给 `gg_hid_output_set_keyboard`；只有快照变化才入队，发送成功仅代表提交，完成事件才移除队头。USB 忙时保留按下/释放顺序，队列容量 32。溢出时清空未提交内容，等待在途完成后先发全释放，再发最新状态；在途 100 ms 未完成则结束会话并请求重连。断线/挂起只保留实体状态，重连先全释放再同步当前快照。

键盘报告固定 8 字节 `[modifier, reserved, key0..key5]`，modifier 恒为 0。`pedal_keys_build(w, s, a, d, keys)` 按 W、S、A、D 顺序填入 key0 起的位置，因此 W+S 与 A+D 会同时占用两个键槽，由游戏解释。

## 默认参数

| 项 | 值 |
|---|---|
| USB | VID/PID `0x303A/0x8267`，序列 `GG-PEDAL-0001` |
| 引脚 | W/S/A/D = 4/5/6/7，LED = 21，低有效 |
| 去抖 | 5 ms |
| 键值 | W/S/A/D = 0x1A/0x16/0x04/0x07 |
| 任务 | core 1、优先级 12、栈 4096 字节、周期 1 ms |

## 输入契约

- 去抖 5 ms，全部松开输出全零键值。
- 四键小于 6 键报告容量，不实现重复按键宏。
- 验证启动已踩住、按住断线、断线松开、重连后的状态同步。
- 独立承力与限位，开关仅负责检测；长线验证上拉、RC 和噪声。

## 调试

JTAG 断点：`gg_app_tick`、`gg_esp_inputs_read`、`gg_hid_output_set_keyboard`、`gg_hid_output_tick`。观察平台启动器的 `s_app.hid`、`s_app.buttons` 和 `s_app.stats`。无 HID 模式始终未连接，不能验证游戏输入。

UART 每 5 s 输出 usb/q/ovf/drop/done/recon。参见 [测试计划](../../docs/TEST_PLAN.md) 与 [调试指南](../../docs/DEBUGGING.md)。
