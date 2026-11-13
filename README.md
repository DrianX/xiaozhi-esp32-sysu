# AI SYSU ESP32

中山大学人工智能学院实验教学中心的 ESP32-S3 AI 语音机器人开发板，基于
[XiaoZhi](https://github.com/78/xiaozhi-esp32) 固件，从 `bread-compact-wifi-lcd`
移植而来。

本文档面向**人和 AI 编程工具**：先看「代码地图」和「修改速查」，再进行改动。
项目级通用规则见 [AGENTS.md](AGENTS.md)，协议和音频等细节见 [docs/](docs/)。

## 快速开始

- SDK：ESP-IDF **v6.0.1+**，推荐 **v6.1**
- 目标芯片：`esp32s3`（8MB PSRAM，16MB Flash）
- 构建方式（VS Code ESP-IDF 插件）：

  1. 用 CH340 的 UART 口连接开发板
  2. `Build Project`
  3. `Flash`（选择对应 COM 口）

- 默认板型和屏幕已在 Kconfig 中固定为 `ai-sysu-esp32` + `ST7789 240x240 7PIN`，
  清除工程重新生成 `sdkconfig` 后不需要再手动选择
- 命令行构建（可选）：

  ```bash
  source /path/to/esp-idf/export.sh
  python scripts/build.py ai-sysu-esp32 --name ai-sysu-esp32
  ```

> 注意：USB 原生口（GPIO19/20）被 UVC 摄像头占用，烧录和串口请走板载 CH340 的 UART。

## 功能开关（config.h）

[config.h](main/boards/ai-sysu-esp32/config.h) 顶部控制可选外设（1=启用，0=关闭）：

| 宏 | 当前 | 说明 |
|---|---|---|
| `AI_SYSU_ENABLE_IMU` | 1 | MPU6050 I2C 姿态（串口遥测输出） |
| `AI_SYSU_ENABLE_TELEMETRY` | 1 | 串口 JSON 遥测，依赖 `AI_SYSU_ENABLE_IMU` |
| `AI_SYSU_ENABLE_MOTORS` | 0 | 双路 DRV8833 电机、MCP 工具、上电自检 |
| `AI_SYSU_ENABLE_SERVOS` | 0 | 4 路 SG90 舵机、MCP 工具、上电自检 |
| `AI_SYSU_ENABLE_CAMERA` | 0 | USB UVC 摄像头和 `self.camera.take_photo` |

### 如何打开某个功能

1. 打开 [main/boards/ai-sysu-esp32/config.h](main/boards/ai-sysu-esp32/config.h)
2. 在文件最顶部找到对应的 `AI_SYSU_ENABLE_*`，把 `0` 改成 `1`
3. 保存后重新 Build（只改功能开关不需要重新配置 CMake）
4. 烧录后按对应功能的接线和使用说明操作

例如只启用电机和舵机：

```c
#define AI_SYSU_ENABLE_IMU 0
#define AI_SYSU_ENABLE_TELEMETRY 0
#define AI_SYSU_ENABLE_MOTORS 1
#define AI_SYSU_ENABLE_SERVOS 1
#define AI_SYSU_ENABLE_CAMERA 0
```

例如只启用姿态 + 串口遥测：

```c
#define AI_SYSU_ENABLE_IMU 1
#define AI_SYSU_ENABLE_TELEMETRY 1
#define AI_SYSU_ENABLE_MOTORS 0
#define AI_SYSU_ENABLE_SERVOS 0
#define AI_SYSU_ENABLE_CAMERA 0
```

注意事项：

- `AI_SYSU_ENABLE_TELEMETRY` 必须同时打开 `AI_SYSU_ENABLE_IMU`
- `MOTOR_BOOT_SELF_TEST` / `SERVO_BOOT_SELF_TEST` 只在对应功能打开时生效
- 摄像头需要 USB UVC Kconfig；本板 `BOARD_AI_SYSU_USB_CAMERA` 已默认选中，
  通常只需打开 `AI_SYSU_ENABLE_CAMERA`
- 也可以用命令行宏覆盖（config.h 使用 `#ifndef`）：`-DAI_SYSU_ENABLE_MOTORS=1`
- 关闭某个功能后，对应驱动不参与编译，也不会初始化硬件或注册 MCP 工具

## 硬件与引脚

| 功能 | 引脚 | 说明 |
|---|---|---|
| 显示 SPI | MOSI=8, CLK=9, DC=6, RST=7 | CS 和背光为 `GPIO_NUM_NC` |
| 麦克风 I2S | WS=41, SCK=45, DIN=42 | Simplex，无外部 codec |
| 扬声器 I2S | DOUT=47, BCLK=38, LRCK=39 | 默认音量 25%（NVS 可覆盖） |
| BOOT 按键 | 0 | 配网/对话 |
| 板载 LED | 48 | WS2812 |
| 电机 A | IN1=12, IN2=13 | DRV8833，LEDC 通道 2/3 |
| 电机 B | IN1=18, IN2=21 | DRV8833，LEDC 通道 4/5 |
| 舵机 1-4 | 14, 15, 16, 17 | SG90，50 Hz，LEDC 通道 0/1/6/7 |
| 电池检测 | 1（ADC1_CH0） | 电池 -> 200k -> GPIO1 -> 100k -> GND |
| 充电检测 | 2 | TP4057 CHRG，开漏，低电平=充电中 |
| MPU6050 | SDA=4, SCL=5 | I2C 400 kHz，地址 0x68，数据走串口遥测 |
| 串口遥测 | UART0（CH340） | 115200 8N1，50 Hz JSON，见 scripts/mpu6050/docs/serial-output-spec.md |
| USB 摄像头 | D-=19, D+=20 | ESP32-S3 原生 USB Host |

完整引脚和参数定义见 [config.h](main/boards/ai-sysu-esp32/config.h)。

## 代码地图

| 内容 | 位置 |
|---|---|
| 板级入口、显示子类、自检、`GetBatteryLevel` / `GetCamera` | [ai_sysu_esp32.cc](main/boards/ai-sysu-esp32/ai_sysu_esp32.cc) |
| 引脚、自检、电池等板级参数 | [config.h](main/boards/ai-sysu-esp32/config.h) |
| 可选外设开关（IMU/电机/舵机/摄像头） | [config.h](main/boards/ai-sysu-esp32/config.h) 顶部 |
| 构建变体、Kconfig 默认值 | [config.json](main/boards/ai-sysu-esp32/config.json)、[Kconfig.projbuild](main/Kconfig.projbuild) |
| 直流电机驱动（DRV8833 / LEDC） | [motor_controller.h](main/boards/ai-sysu-esp32/motor_controller.h)、[motor_controller.cc](main/boards/ai-sysu-esp32/motor_controller.cc) |
| 舵机驱动（SG90 / 50 Hz PWM） | [servo_controller.h](main/boards/ai-sysu-esp32/servo_controller.h)、[servo_controller.cc](main/boards/ai-sysu-esp32/servo_controller.cc) |
| 电池 ADC / 充电检测公共驱动 | [adc_battery_monitor.cc](main/boards/common/adc_battery_monitor.cc) |
| MPU6050 姿态驱动（I2C + Mahony 四元数滤波） | [mpu6050.h](main/boards/ai-sysu-esp32/mpu6050.h)、[mpu6050.cc](main/boards/ai-sysu-esp32/mpu6050.cc) |
| 上位机串口遥测规范 | [scripts/mpu6050/docs/serial-output-spec.md](scripts/mpu6050/docs/serial-output-spec.md) |
| 上位机仪表盘（Web Serial + 3D） | [scripts/mpu6050/](scripts/mpu6050/)，入口 [start.bat](scripts/mpu6050/start.bat) |
| 上位机示例固件（Arduino） | [scripts/mpu6050/firmware/esp32_mpu6050_json/](scripts/mpu6050/firmware/esp32_mpu6050_json/) |
| 固件端遥测发送 | [ai_sysu_esp32.cc](main/boards/ai-sysu-esp32/ai_sysu_esp32.cc) 的 `SendImuTelemetry()` |
| USB UVC 摄像头公共驱动 | [esp_video.cc](main/boards/common/esp_video.cc) |
| 摄像头 MCP 工具 | [mcp_server.cc](main/mcp_server.cc) 中 `self.camera.take_photo` |
| 显示 / 状态栏 | [lcd_display.cc](main/display/lcd_display.cc)、[lvgl_display.cc](main/display/lvgl_display/lvgl_display.cc) |
| 默认音量 | [audio_codec.h](main/audio/audio_codec.h) 的 `output_volume_` |
| 项目级代码规则 | [AGENTS.md](AGENTS.md) |

## 修改速查

| 想改什么 | 改哪里 |
|---|---|
| 启用/关闭 IMU、电机、舵机、摄像头 | [config.h](main/boards/ai-sysu-esp32/config.h) 顶部的 `AI_SYSU_ENABLE_*`，1=启用，0=关闭 |
| 换引脚 / 换外设 | [config.h](main/boards/ai-sysu-esp32/config.h)。电机/舵机还要确认 [ai_sysu_esp32.cc](main/boards/ai-sysu-esp32/ai_sysu_esp32.cc) 里 `InitializeMotor()` / `InitializeServo()` 的 LEDC 通道不冲突 |
| 改默认音量 | [audio_codec.h](main/audio/audio_codec.h) 的 `output_volume_`；NVS 里存过音量时以 NVS 为准 |
| 改电机速度/自检 | `MOTOR_TEST_SPEED`、`MOTOR_TEST_DURATION_MS`、`MOTOR_BOOT_SELF_TEST`；运行期走 `self.motor_a.*` / `self.motor_b.*` |
| 改舵机行程/中位 | `SERVO_PWM_MIN_PULSE_US`、`SERVO_PWM_MAX_PULSE_US`、`SERVO_INIT_ANGLE`、`SERVO_TEST_*` |
| 改低电量阈值/标定 | `BATTERY_LOW_LEVEL`、`BATTERY_VOLTAGE_SCALE`、`BATTERY_CHARGING_ACTIVE_LOW` |
| 改 MPU6050 引脚/地址 | `IMU_SDA_GPIO`、`IMU_SCL_GPIO`、`IMU_I2C_ADDR`、`IMU_I2C_SPEED_HZ`、`IMU_SAMPLE_INTERVAL_MS` |
| 改遥测波特率/开关 | `TELEMETRY_ENABLE`、`TELEMETRY_UART_NUM`、`TELEMETRY_BAUD_RATE`、`TELEMETRY_SILENCE_LOGS` |
| 改姿态轴向/滤波 | 轴向、符号和 Mahony 参数在 [mpu6050.cc](main/boards/ai-sysu-esp32/mpu6050.cc) 的 `Update()` |
| 改充电图标颜色 | [ai_sysu_esp32.cc](main/boards/ai-sysu-esp32/ai_sysu_esp32.cc) 的 `AiSysuLcdDisplay::UpdateStatusBar()`（当前绿色 `0x00C853`） |
| 改拍照预览大小 | `AiSysuLcdDisplay::SetPreviewImage()`（当前屏幕 90%） |
| 换屏幕型号 | Kconfig 的 `DISPLAY_LCD_TYPE` 默认值；`config.json` 的 `sdkconfig_append` |
| 换默认板型 | [Kconfig.projbuild](main/Kconfig.projbuild) 中 `choice BOARD_TYPE` 的默认值 |
| 加 MCP 工具 | 在对应控制器构造函数里 `McpServer::GetInstance().AddTool(...)`，参考 [lamp_controller.h](main/boards/common/lamp_controller.h) |
| 加新的板级 `.cc` 文件 | 放 `main/boards/ai-sysu-esp32/` 后需重新配置 CMake（`idf.py reconfigure` 或 Full Clean） |

## 关键配置项（config.h）

- 电机：`MOTOR_A_IN1/IN2_GPIO`、`MOTOR_B_IN1/IN2_GPIO`、`MOTOR_A/B_NSLEEP_GPIO`
- 自检：`MOTOR_BOOT_SELF_TEST`、`MOTOR_TEST_SPEED=80`、`MOTOR_TEST_DURATION_MS=1500`
- 舵机：`SERVO_1..4_GPIO`、`SERVO_COUNT=4`、`SERVO_PWM_MIN/MAX_PULSE_US=500/2500`、
  `SERVO_INIT_ANGLE=90`、`SERVO_BOOT_SELF_TEST=1`、`SERVO_TEST_STEP_MS=400`
- 电池：`BATTERY_ADC_UNIT=ADC_UNIT_1`、`BATTERY_ADC_CHANNEL=ADC_CHANNEL_0`、
  `BATTERY_UPPER/LOWER_RESISTOR=200k/100k`、`BATTERY_CHARGING_PIN=GPIO2`、
  `BATTERY_CHARGING_ACTIVE_LOW=1`、`BATTERY_LOW_LEVEL=15`、`BATTERY_VOLTAGE_SCALE=1.03`
- MPU6050：`IMU_I2C_PORT=0`、`IMU_SDA_GPIO=4`、`IMU_SCL_GPIO=5`、`IMU_I2C_ADDR=0x68`、
  `IMU_I2C_SPEED_HZ=400000`、`IMU_SAMPLE_INTERVAL_MS=20`（50 Hz）
- 遥测：`AI_SYSU_ENABLE_TELEMETRY=1`（在 config.h 顶部，需同时打开 IMU）、
  `TELEMETRY_UART_NUM=0`、`TELEMETRY_BAUD_RATE=115200`、`TELEMETRY_SILENCE_LOGS=1`

## 功能与语音控制

上电约 1 秒后自动执行硬件自检（`MOTOR_BOOT_SELF_TEST` / `SERVO_BOOT_SELF_TEST` 可关闭）：

1. 电机 A：正转 1.5s -> 反转 1.5s -> 停止
2. 电机 B：正转 1.5s -> 反转 1.5s -> 停止
3. 舵机 1-4：逐个 45° -> 135° -> 回中位 90°

联网后可通过语音/大模型调用 MCP 工具：

- 电机：`self.motor_a.spin` / `self.motor_b.spin`（`speed` -100~100，`duration_ms`）、
  `.stop`、`.get_state`
- 舵机：`self.servo.set_angle`（`channel` 1-4，`angle` 0-180）、
  `self.servo.get_angles`、`self.servo.center_all`
- 摄像头：`self.camera.take_photo(question)`，示例语音“看看前面是什么”
- 电池/音量/屏幕：`self.get_device_status`、`self.audio_speaker.set_volume`、
  `self.screen.set_theme`

MPU6050 数据只通过串口遥测输出（50 Hz JSON），**不在屏幕上显示**。

姿态由 **Mahony 四元数滤波** 计算：陀螺负责动态跟踪，加速度计修正 pitch/roll；
yaw 只能靠陀螺积分，没有磁力计，长时间仍会缓慢漂移（上电时会做一次零偏校准，
校准期间保持静止）。需要绝对 yaw 请加磁力计（如 QMC5883L），改用 9 轴融合。

## 上位机串口遥测

协议规范见 [scripts/mpu6050/docs/serial-output-spec.md](scripts/mpu6050/docs/serial-output-spec.md)，
上位机仪表盘在 [scripts/mpu6050/](scripts/mpu6050/)：

1. 在 `config.h` 打开 `AI_SYSU_ENABLE_IMU=1`、`AI_SYSU_ENABLE_TELEMETRY=1`
2. 编译烧录，**关闭 VS Code 串口监视器**（Web Serial 需要独占串口）
3. 双击 [scripts/mpu6050/start.bat](scripts/mpu6050/start.bat)，会启动本地静态服务器并打开 Chrome/Edge
4. 点击“选择串口”，选择 ESP32 的 CH340 串口，波特率 **115200**

仪表盘的详细架构、3D 模型和验证清单见
[scripts/mpu6050/README.md](scripts/mpu6050/README.md)。

固件端输出：

- 串口：UART0（板载 CH340），**115200 8N1**，无流控（网页波特率也要设成 115200）
- **50 Hz**，每行一个完整 JSON，以 `\n` 结尾
- 字段：`roll/pitch/yaw`（deg）、`ax/ay/az`（g）、`gx/gy/gz`（°/s）、`temp`（°C）
- 遥测开始后全局日志级别降到 WARN，避免 INFO 日志混入数据流；需要日志时把
  `TELEMETRY_SILENCE_LOGS` 改成 0
- 上位机通过 Web Serial 连接时需**独占串口**，先关闭 VS Code 串口监视器
- 当前使用 115200；50 Hz 下 JSON 约占 70% 带宽。如果网页出现丢帧，可以把
  `IMU_SAMPLE_INTERVAL_MS` 调大（例如 25 ms，40 Hz），或把 `TELEMETRY_BAUD_RATE`
  提高到 921600 / 1152000 并同步修改网页波特率

示例帧：

```json
{"roll":1.25,"pitch":-3.40,"yaw":128.60,"ax":0.018,"ay":-0.012,"az":0.997,"gx":0.4,"gy":-0.2,"gz":1.1,"temp":31.5}
```

坐标约定：X+ 前方、Y+ 左方、Z+ 上方；roll 右侧下沉为正、pitch 机头抬起为正、
yaw 左转为正。pitch 已按规范（机头抬起为正）输出。

## USB 摄像头

- 使用 1MP USB UVC 免驱摄像头，走 ESP32-S3 原生 USB Host：
  - 摄像头 `D-`/`DM` -> **GPIO19**
  - 摄像头 `D+`/`DP` -> **GPIO20**
  - 摄像头 5V -> 5V，GND -> GND（必须单独供 5V，数据线尽量短）
- 接线已在实物验证通过；DM/DP 接反时串口会报
  `ENUM: CHECK_SHORT_DEV_DESC FAILED`，摄像头无法枚举
- 板级 Kconfig 默认已打开 USB UVC 和 MJPEG 输入（`BOARD_AI_SYSU_USB_CAMERA`），
  自动选择 `CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE` 和
  `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT`
- 换其他 UVC 摄像头无需改代码：驱动自动枚举并优先选 **MJPEG**；也支持
  YUYV/UYVY/RGB565/RGB24/GREY，不支持 H.264/H.265
- 分辨率/FPS 使用摄像头默认值，不主动协商；建议默认分辨率 ≤ 1280×720
- 正常启动日志：

  ```text
  I (xxx) usb_uvc_device: Waiting for UVC device to be enumerated...
  I (xxx) EspVideo: Camera init success
  I (xxx) MCP: Add tool: self.camera.take_photo
  ```

- 未插摄像头时初始化会等待约 10 秒后继续启动；枚举异常可打开
  `CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE` 查看日志

## 电池与充电

- 右上角显示 8 档电量图标；充电时显示带闪电的**绿色**电池图标
- 电量 ≤ `BATTERY_LOW_LEVEL`（默认 15%）且未充电时，状态栏提示“电量低，请充电”
- TP4057 CHRG 默认低电平表示充电；若模块极性相反，把
  `BATTERY_CHARGING_ACTIVE_LOW` 改成 0
- GPIO2 初始化时启用内部上拉；若模块板上有上拉到 5V 的电阻/LED，确认
  GPIO2 不超过 3.3V
- 电压标定：`BATTERY_VOLTAGE_SCALE` 默认 1.03；更换电阻或重新标定时按
  `实际电压 / 显示电压` 调整

## 构建注意事项

- **新增板级 `.cc` 文件后必须重新配置 CMake**（`file(GLOB ...)` 只在配置阶段
  扫描目录）：执行 `idf.py reconfigure`，或在 VS Code 里做一次 Full Clean
- 板型和屏幕默认值在 [Kconfig.projbuild](main/Kconfig.projbuild) 中，清除工程
  重新生成 `sdkconfig` 后仍会生效
- 不要手改生成/第三方目录：`build/`、`releases/`、`managed_components/`、
  `components/`、`sdkconfig*`、`main/assets/lang_config.h`、生成的 mmap 头文件
- 修改 C/C++ 文件后按仓库 `.clang-format` 格式化
- 主机侧测试（不涉及上板）：

  ```bash
  python -m unittest discover -s scripts/tests -v
  ```

- 编译成功不等于硬件验证；涉及音频、摄像头、电机、电源的改动需要上板确认

## 排障

| 现象 | 排查方向 |
|---|---|
| 摄像头无枚举，`CHECK_SHORT_DEV_DESC FAILED` | DM/DP 接反（D-=GPIO19，D+=GPIO20）、5V/GND、数据线过长 |
| 电机不转 | DRV8833 `nSLEEP` 接 3.3V、`VM` 电机电源、与 ESP32 共地 |
| 充电图标不出现/一直充电 | `BATTERY_CHARGING_ACTIVE_LOW` 极性与模块是否一致 |
| MPU6050 初始化失败 / 串口没有遥测数据 | 地址是 0x68 还是 0x69（AD0）、SDA/SCL 是否接反、模块 3.3V 供电和上拉；看串口 `MPU6050` 日志 |
| 网页收不到遥测数据 | 固件和网页波特率必须一致（当前 115200）、选对 COM 口、先关闭 VS Code 串口监视器 |
| 遥测数据中混有日志 | 确认 `TELEMETRY_SILENCE_LOGS=1`；遥测开始前的启动日志会被网页忽略 |
| 白屏/重启 | 先看串口复位原因：`Reset reason: 9` 或 `Brownout` 是供电/接线，`4` 是 panic（保留 backtrace） |
| 电量显示偏差大 | 分压电阻实际值、GPIO1 对地 100nF、`BATTERY_VOLTAGE_SCALE` |
| 默认音量不生效 | NVS 里已有 `audio/output_volume`，语音调一次或清 NVS |
| 新增 `.cc` 后链接报 undefined reference | 执行 `idf.py reconfigure` 或 Full Clean |
| 摄像头插着无法烧录 | 烧录走 CH340 UART，不要用被占用的原生 USB |

## 给 AI 编程工具的约定

- 先读 [AGENTS.md](AGENTS.md)；它是项目级规则的权威来源
- 板级逻辑放 `main/boards/ai-sysu-esp32/`；可复用的硬件/网络逻辑放
  `main/boards/common/`；不要把板级行为塞进核心模块
- 一个构建只能有一个 `DECLARE_BOARD(...)`；本板是 `AiSysuEsp32`
- 运行时状态变化走 `Application::SetDeviceState()` 和状态机
- 回调可能在主任务之外执行：用 `Application::Schedule()` 或事件位调度应用层修改
- 不要阻塞主事件循环或音频任务；音频路径避免无界队列和反复大块分配
- 网络输入要校验；保持 `cJSON` 所有权语义
- NVS key 是对外持久化接口，修改时必须考虑迁移
- 目标相关能力用 Kconfig/组件规则保护，不要假设所有芯片都有 PSRAM
- 提交前只格式化改动过的 C/C++ 文件，避免无关的大面积格式化
