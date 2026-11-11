# AI SYSU ESP32

自定义硬件板（ai-sysu-esp32），基于 `bread-compact-wifi-lcd` 移植而来。

## 硬件

- 主控：ESP32-S3
- 网络：Wi-Fi
- 显示：SPI ST7789 240x240 7PIN
- 音频：I2S 直连（无外部 codec），Simplex 模式
- 按键：BOOT 按键
- LED：板载 LED
- 直流电机：DRV8833 双路驱动（LEDC PWM 调速）
  - 通道 A：IN1=GPIO12，IN2=GPIO13
  - 通道 B：IN1=GPIO18，IN2=GPIO21
- 舵机：4 路 SG90（50 Hz PWM）
  - 1 号=GPIO14，2 号=GPIO15，3 号=GPIO16，4 号=GPIO17

## 状态

当前 IO 引脚暂与 `bread-compact-wifi-lcd` 保持一致，待根据实际原理图修改
`config.h` 中的引脚定义。

## 硬件自检

上电约 1 秒后自动执行一次自检，串口可看到对应的 `self-test` 日志：

1. 电机 A：正转 1.5 秒 -> 反转 1.5 秒 -> 停止
2. 电机 B：正转 1.5 秒 -> 反转 1.5 秒 -> 停止
3. 舵机 1-4：逐个转到 45° -> 135° -> 回中位 90°

修改 `config.h` 中的 `MOTOR_BOOT_SELF_TEST`、`SERVO_BOOT_SELF_TEST`
可分别关闭自检；`MOTOR_TEST_SPEED`、`MOTOR_TEST_DURATION_MS`、
`SERVO_TEST_ANGLE_LOW/HIGH`、`SERVO_TEST_STEP_MS` 可调整测试参数。

DRV8833 的 `nSLEEP` 悬空时芯片不会输出，直接接 3.3V 即可；若由 GPIO
控制，把 `MOTOR_A_NSLEEP_GPIO` / `MOTOR_B_NSLEEP_GPIO` 改成对应引脚。

联网后也可以通过 MCP 工具控制：

- `self.motor_a.spin` / `self.motor_b.spin`：`speed` 为 -100~100
  （正数正转、负数反转），`duration_ms` 为旋转时长，0 表示持续旋转
- `self.motor_a.stop` / `self.motor_b.stop`：立即停止
- `self.motor_a.get_state` / `self.motor_b.get_state`：读取当前速度与方向
- `self.servo.set_angle`：`channel` 为 1-4，`angle` 为 0-180 度
- `self.servo.get_angles`：读取 4 路舵机当前角度
- `self.servo.center_all`：所有舵机回中位（90 度）

## 构建

直接使用esp-idf工具，在vscode下方点击build project即可

使用 VS Code ESP-IDF 插件直接编译时，`main/Kconfig.projbuild` 已把本板
和 ST7789 240x240 7PIN 屏设为默认选项，清除工程重新生成 `sdkconfig`
后无需再手动选择。
