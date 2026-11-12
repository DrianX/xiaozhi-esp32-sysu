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
- 电池：3.7V 1500mAh 锂电池，充电限制电压 4.2V
  - 电量检测：电池 -> 200k -> GPIO1(ADC1_CH0) -> 100k -> GND
  - 充电状态：TP4057 的 CHRG -> GPIO2（开漏，充电时拉低）
- 摄像头：1MP USB UVC 免驱摄像头（ESP32-S3 原生 USB，D-/DM=GPIO19，D+/DP=GPIO20）

## 状态

板级引脚和功能已完成验证，完整引脚定义见 `main/boards/ai-sysu-esp32/config.h`。

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

## 电池与充电

- 屏幕右上角实时显示电池图标（8 档电量），充电时显示带闪电的绿色电池图标
- 电量低于 `BATTERY_LOW_LEVEL`（默认 15%）且未充电时，状态栏弹出
  “电量低，请充电”提示
- 充电状态来自 TP4057 的 CHRG 引脚，默认低电平表示正在充电；若模块
  极性相反，把 `BATTERY_CHARGING_ACTIVE_LOW` 改成 0
- 上电时会自动启用 GPIO2 的内部上拉；如果模块板上已有上拉到 5V 的
  电阻/LED，请确认 GPIO2 电压不超过 3.3V
- 电压标定：`BATTERY_VOLTAGE_SCALE` 默认 1.03（万用表 4.12V 对应固件
  4.00V）。更换电阻或重新标定时，按 `实际电压 / 显示电压` 调整

## USB 摄像头

- 使用 1MP USB UVC 免驱摄像头（替代 OV2640/OV3660 的 USB 款），走 ESP32-S3 原生 USB Host：
  - 摄像头 `D-`/`DM` -> GPIO19
  - 摄像头 `D+`/`DP` -> GPIO20
  - 摄像头 5V -> 5V，GND -> GND（摄像头必须单独供 5V，数据线尽量短）
- 该接线已在实物上验证通过。注意 ESP32-S3 的 `D-` 是 GPIO19、`D+` 是 GPIO20，
  两者接反时串口会报 `ENUM: CHECK_SHORT_DEV_DESC FAILED`，摄像头无法枚举
- 摄像头占用原生 USB 后，烧录/串口请继续使用板载 CH340 的 UART 口
- 板级 Kconfig 默认已打开 USB UVC 和 MJPEG 输入（`BOARD_AI_SYSU_USB_CAMERA`），
  编译时会自动选择 `CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE` 和
  `CONFIG_XIAOZHI_CAMERA_ALLOW_JPEG_INPUT`
- 联网后对设备说“看看前面是什么”，大模型会调用 `self.camera.take_photo`，
  拍照后通过服务器的 vision 接口识别并回答
- 正常启动日志：

  ```
  I (xxx) usb_uvc_device: Waiting for UVC device to be enumerated...
  I (xxx) EspVideo: Camera init success
  I (xxx) MCP: Add tool: self.camera.take_photo
  ```

- 未插入摄像头时，初始化会等待约 10 秒后继续启动；识别异常时可打开
  `CONFIG_XIAOZHI_ENABLE_CAMERA_DEBUG_MODE` 查看 USB 枚举和格式协商日志

## 构建

直接使用esp-idf工具，在vscode下方点击build project即可

使用 VS Code ESP-IDF 插件直接编译时，`main/Kconfig.projbuild` 已把本板
和 ST7789 240x240 7PIN 屏设为默认选项，清除工程重新生成 `sdkconfig`
后无需再手动选择。
