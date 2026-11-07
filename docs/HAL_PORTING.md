# HAL 分层与新平台移植

> 默认脚踏已迁移至 STM32F103C8T6。当前目标的分层、接线、构建、测试和移植规则见 [STM32 脚踏指南](STM32_PEDAL.md)。下文 ESP32 脚踏内容仅适用于保留的旧后端。

## 当前边界

```text
平台启动入口（ESP-IDF app_main / 其他 MCU main / 主机事件循环）
        ↓ 注入 gg_hal_t
gg_app：应用周期、输入去抖、脚踏映射、事件分发、恢复调度、状态灯策略、统计
        ↓
gg_core：校准、瞄准、枪体状态机、HID 快照队列和会话算法
        ↓ 通过 HAL 回调返回底层
platform/<平台>：GPIO/ADC、IMU、时钟、RTOS/裸机调度、USB 栈、LED、日志
```

`firmware/components/gg_app`、`gg_core` 只使用标准 C11 和数学库，没有 ESP-IDF、FreeRTOS、GPIO、SPI、TinyUSB 头文件。平台通过 `gg_hal_t` 注入能力；应用实例持有上下文，不依赖全局设备对象。驱动句柄、引脚、电平、寄存器、USB 物理端点、任务创建全部留在平台层。

ESP32 当前实现：

| 路径 | 职责 |
|---|---|
| `firmware/gun-controller/main/main.c` | 选择枪体产品，调用 ESP32 启动器 |
| `firmware/pedal-controller/main/main.c` | 选择脚踏产品，调用 ESP32 启动器 |
| `firmware/platform/esp32/common` | 时钟、FreeRTOS 调度、GPIO 帮助函数、日志、任务启动 |
| `firmware/platform/esp32/gun/board.c` | 枪体 HAL 函数表，将 IMU/按键/USB 驱动接入应用 |
| `firmware/platform/esp32/pedal/board.c` | 脚踏 HAL 函数表，不需要 IMU |
| `firmware/platform/esp32/{gun,pedal}/board_config.h` | 硬件引脚、USB 标识、传感器配置；不存应用增益和键码 |
| `firmware/platform/esp32/gun/icm42688*` | ICM-42688 寄存器及解析，不进入可移植应用依赖 |
| `firmware/platform/esp32/components/gg_usb` | ESP-IDF/TinyUSB 单任务传输及物理接口转换 |
| `firmware/components/gg_app/include/gg_hal.h` | 平台必须遵循的标准 C 接口 |
| `firmware/components/gg_core/include/gg_transport.h` | 逻辑传输事件，无 USB 栈依赖 |

## 替换平台或硬件的步骤

1. 新建 `firmware/platform/<新平台>`，实现 HAL 函数表。采用该平台自己的入口、编译器、链接脚本、调度器和驱动。
2. 按下表映射接口，使用相同 `gg_app_default_config(GG_DEVICE_GUN/PEDAL)` 或独立的产品配置。应用增益和去抖属于产品参数，不应塞进硬件引脚配置。
3. 在平台启动阶段持有 `gg_app_t` 和 HAL 上下文，调用一次 `gg_app_init`；成功后由定时循环调用 `gg_app_tick`，或提供 `wait_next` 并调用 `gg_app_run`。
4. 将纯 C 的 `gg_core` 和 `gg_app` 加入新平台构建。无需修改这两层源码。移植平台仍需自己的启动/链接/构建配置；接口替换不等于自动支持一款未验证的 MCU。
5. 先跑主机回归，再验证真实按键、USB 会话、采样时序、轴符号、恢复和延迟。只有 ESP32 后端完成了编译验证，其他 MCU 尚未实机验证。

换同平台的 IMU：只换驱动及 `board.c` 中 IMU 回调；换按键接线：只换 `board_config.h`/引脚映射；换 WS2812、普通 LED 或无指示灯：只改 `set_indicator` 实现。上层不直接调用 GPIO 或认识具体传感器型号。

## 接口契约

| HAL 回调 | 必须遵守的规则 |
|---|---|
| `init` | 初始化输入、指示灯、传输。允许开机阻塞；失败前清理自己已申请的资源。不能改应用状态 |
| `now_us` | 单调 64 位微秒；IMU 时间戳必须同一时基。不能传 RTOS tick 或未扩展的 32 位计数 |
| `wait_next` | 可选。按传入周期等待，超期重新同步，不连续补跑多个假周期。外部调度 `tick` 时可为空 |
| `read_inputs` | 返回逻辑位掩码，1 表示按下；HAL 处理接线、电平或模拟阈值，上层统一软件去抖。失败返回 false，上层释放所有主机按键且不合成 PROFILE 释放手势 |
| `poll_event` | 非阻塞 FIFO，每次取一项。事件含逻辑接口、提交时会话/编号；不能在底层回调直接调用上层 |
| `event_overflows` | 线程安全的累计计数；丢失通知不得无声发生，上层检测后请求重连 |
| `submit_report` | 返回前复制报告，不能保存栈指针。true 表示传输层接管；之后须回 COMPLETE 或 FAILED。false 表示未接管。每逻辑接口最多一个在途 |
| `reconnect` | 异步请求。必须有断开及恢复事件，不允许直接偷偷修改上层会话 |
| `init_imu` | 枪体必需。失败是可恢复 IMU 故障，按键/传输仍工作；脚踏可为空 |
| `read_imu` | 只对真正新样本返回 NEW；无数据、IO 错、无效数据分开。dps/g、机体坐标 +X 枪口/+Y 左/+Z 上，挂载方向和比例在适配层归一化；上层还拒绝 NaN/Inf |
| `start_imu_recovery` | 只启动恢复，不在 1 kHz 路径长时间等待 |
| `poll_imu_recovery` | false 表示仍进行；true 表示完成并通过 `ok` 返回结果。正确管理总线资源，不反复泄漏 |
| `set_indicator` | 可选。只实现亮/灭，闪烁优先级由上层决定 |
| `log_stats` | 可选。接收结构化快照，自己选择 UART/RTT/文件等后端；不可长期阻塞周期任务 |

逻辑接口为 `GG_HID_ITF_MOUSE=0`、`GG_HID_ITF_KEYBOARD=1`，与硬件 USB 接口号无关。报告内容仍是本产品的相对 HID 鼠标/键盘协议；替换 USB 栈或改用其他传输时，HAL 需实现这些语义。连接/恢复先产生新会话；旧报告完成事件必须保留旧 token，不能确认新队列。

接口版本为 `GG_HAL_ABI_VERSION`；这是源码级 C 接口，不承诺跨编译器的二进制插件 ABI。HAL 上下文和资源必须比应用实例长寿；初始化一次后保持绑定，运行期间不能替换函数表或再次初始化已占用硬件的实例。运行时热切换和完整 deinit 生命周期不属于本次实现。

## 平台入口示例

```c
#include "gg_app.h"

static gg_app_t app;

int main(void)
{
    gg_hal_t hal = my_board_hal(); /* 新平台自己的适配实现 */
    gg_app_config_t cfg = gg_app_default_config(GG_DEVICE_GUN);
    if (!gg_app_init(&app, &hal, &cfg)) return 1;
    gg_app_run(&app); /* 裸机/RTOS 适配 wait_next；或自行周期调用 tick */
    return 0;
}
```

`tests/test_app_hal.c` 是可运行的适配参考：同一应用分别注入普通输入与不同原始接线的输入 HAL，验证报告相同、实例隔离、初始按住、输入失败释放、IMU 故障恢复、无效浮点拒绝和结构化日志。无需安装任何 ESP SDK。

## 无 SDK 的构建

本轮实际验证：核心 3232 项、USB 传输 55 项、完整应用 HAL 38 项检查全部零失败；独立根 CMake 的三项 CTest 全部通过。ESP32 四种构建结果和日志见 TEST_PLAN.md。主机测试不会替代真实板卡验收。

使用系统 C 编译器及 CMake/Ninja：

```powershell
cmake -S . -B build-host -G Ninja
cmake --build build-host
ctest --test-dir build-host --output-on-failure
```

根 CMake 生成 `gg_core_portable`、`gg_app_portable` 两个库；`-DBUILD_TESTING=OFF` 可只构建库，供新平台工程集成。根目录是主机/可移植构建，ESP32 固件仍从两个 `firmware/*-controller` 工程构建。也可运行 `tests/run_host_tests.ps1` 自动查找本机 GCC。

