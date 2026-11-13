#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

// ============================================================================
// 板级功能开关
//   1 = 启用，0 = 关闭；按实际硬件在编译前设置。
//   关闭后对应驱动不参与编译，也不会初始化硬件。
//   也可以用 -DAI_SYSU_ENABLE_xxx=1 在命令行覆盖。
// ============================================================================

// MPU6050 六轴姿态传感器（I2C：SDA=GPIO4，SCL=GPIO5）
// 启用后以 50 Hz 读取姿态，供串口遥测使用
#ifndef AI_SYSU_ENABLE_IMU
#define AI_SYSU_ENABLE_IMU 1
#endif

// 上位机串口遥测（格式见 scripts/mpu6050/docs/serial-output-spec.md）
// 依赖 AI_SYSU_ENABLE_IMU；启用后 UART0 以 115200 输出 50 Hz JSON
#ifndef AI_SYSU_ENABLE_TELEMETRY
#define AI_SYSU_ENABLE_TELEMETRY 1
#endif

// DRV8833 双路直流电机（通道 A：GPIO12/13，通道 B：GPIO18/21）
// 启用后注册 self.motor_a.* / self.motor_b.* MCP 工具，可选上电自检
#ifndef AI_SYSU_ENABLE_MOTORS
#define AI_SYSU_ENABLE_MOTORS 0
#endif

// 4 路 SG90 舵机（1-4 号：GPIO14/15/16/17）
// 启用后注册 self.servo.* MCP 工具，可选上电扫动自检
#ifndef AI_SYSU_ENABLE_SERVOS
#define AI_SYSU_ENABLE_SERVOS 0
#endif

// USB UVC 摄像头（ESP32-S3 原生 USB：D-=GPIO19，D+=GPIO20）
// 启用后注册 self.camera.take_photo，语音可拍照识别
// 需要 USB UVC Kconfig 支持（本板 BOARD_AI_SYSU_USB_CAMERA 已默认选中）
#ifndef AI_SYSU_ENABLE_CAMERA
#define AI_SYSU_ENABLE_CAMERA 0
#endif

#include <driver/gpio.h>
#include <esp_adc/adc_oneshot.h>

// ============================================================================
// 音频（I2S 直连，无外部 codec）
// ============================================================================
#define AUDIO_INPUT_SAMPLE_RATE  16000  // 麦克风采样率
#define AUDIO_OUTPUT_SAMPLE_RATE 24000  // 扬声器采样率

// 如果使用 Duplex I2S 模式，请注释下面一行
#define AUDIO_I2S_METHOD_SIMPLEX

#ifdef AUDIO_I2S_METHOD_SIMPLEX

// Simplex：麦克风和扬声器各占一组 I2S
#define AUDIO_I2S_MIC_GPIO_WS   GPIO_NUM_41  // 麦克风 WS/LRCK
#define AUDIO_I2S_MIC_GPIO_SCK  GPIO_NUM_45  // 麦克风 BCLK
#define AUDIO_I2S_MIC_GPIO_DIN  GPIO_NUM_42  // 麦克风数据输入
#define AUDIO_I2S_SPK_GPIO_DOUT GPIO_NUM_47  // 扬声器数据输出
#define AUDIO_I2S_SPK_GPIO_BCLK GPIO_NUM_38  // 扬声器 BCLK
#define AUDIO_I2S_SPK_GPIO_LRCK GPIO_NUM_39  // 扬声器 WS/LRCK

#else

// Duplex：麦克风和扬声器共用一组 I2S（本板未使用）
#define AUDIO_I2S_GPIO_WS GPIO_NUM_4
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_5
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_6
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_7

#endif

// ============================================================================
// 按键与指示灯
// ============================================================================
#define BUILTIN_LED_GPIO        GPIO_NUM_48  // 板载 WS2812
#define BOOT_BUTTON_GPIO        GPIO_NUM_0   // BOOT 按键：配网/对话
#define TOUCH_BUTTON_GPIO       GPIO_NUM_NC  // 未使用触摸按键
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC  // 未使用音量加按键
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC  // 未使用音量减按键

// ============================================================================
// LCD（SPI ST7789 240x240 7PIN）
// ============================================================================
#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC  // 无独立背光控制脚
#define DISPLAY_MOSI_PIN      GPIO_NUM_8   // SPI MOSI
#define DISPLAY_CLK_PIN       GPIO_NUM_9   // SPI CLK
#define DISPLAY_DC_PIN        GPIO_NUM_6   // 数据/命令选择
#define DISPLAY_RST_PIN       GPIO_NUM_7   // 复位
#define DISPLAY_CS_PIN        GPIO_NUM_NC  // 片选常低


#ifdef CONFIG_LCD_ST7789_240X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X320_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_170X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   170
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  35
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_172X320
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   172
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  34
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X280
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  280
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  20
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7789_240X240_7PIN
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 3
#endif

#ifdef CONFIG_LCD_ST7789_240X135
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  135
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY true
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  40
#define DISPLAY_OFFSET_Y  53
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X160
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  160
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7735_128X128
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  128
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR  false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  32
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ST7796_320X480_NO_IPS
#define LCD_TYPE_ST7789_SERIAL
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  480
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_ILI9341_240X320_NO_IPS
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    false
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_GC9A01_240X240
#define LCD_TYPE_GC9A01_SERIAL
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  240
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_BGR
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif

#ifdef CONFIG_LCD_CUSTOM
#define DISPLAY_WIDTH   240
#define DISPLAY_HEIGHT  320
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y false
#define DISPLAY_SWAP_XY false
#define DISPLAY_INVERT_COLOR    true
#define DISPLAY_RGB_ORDER  LCD_RGB_ELEMENT_ORDER_RGB
#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false
#define DISPLAY_SPI_MODE 0
#endif


// MCP 示例：控制一个灯（GPIO_NUM_NC 表示本板不使用）
#define LAMP_GPIO GPIO_NUM_NC

// DRV8833 双路直流电机驱动，每路电机占用 IN1/IN2 两路 PWM
// 通道 A
#define MOTOR_A_IN1_GPIO GPIO_NUM_12  // A 通道 IN1
#define MOTOR_A_IN2_GPIO GPIO_NUM_13  // A 通道 IN2
// 通道 B
#define MOTOR_B_IN1_GPIO GPIO_NUM_18  // B 通道 IN1
#define MOTOR_B_IN2_GPIO GPIO_NUM_21  // B 通道 IN2
// 若 nSLEEP 由 GPIO 控制则填写引脚号；nSLEEP 直接接 3.3V 时保持 GPIO_NUM_NC
#define MOTOR_A_NSLEEP_GPIO GPIO_NUM_NC  // A 通道 nSLEEP
#define MOTOR_B_NSLEEP_GPIO GPIO_NUM_NC  // B 通道 nSLEEP

// 上电自检：正转 -> 反转 -> 停止，用于台架验证；量产时可置 0 关闭
#define MOTOR_BOOT_SELF_TEST 1       // 1=上电自检，0=关闭
#define MOTOR_TEST_SPEED 80          // 自检速度百分比
#define MOTOR_TEST_DURATION_MS 1500  // 每个方向持续时间（ms）

// SG90 舵机，接线顺序为 GPIO14/15/16/17
#define SERVO_1_GPIO GPIO_NUM_14  // 1 号舵机
#define SERVO_2_GPIO GPIO_NUM_15  // 2 号舵机
#define SERVO_3_GPIO GPIO_NUM_16  // 3 号舵机
#define SERVO_4_GPIO GPIO_NUM_17  // 4 号舵机
#define SERVO_COUNT 4             // 舵机数量

// SG90 脉宽范围（0-180 度）
#define SERVO_PWM_MIN_PULSE_US 500   // 0 度对应脉宽（us）
#define SERVO_PWM_MAX_PULSE_US 2500  // 180 度对应脉宽（us）
#define SERVO_INIT_ANGLE 90          // 上电初始角度

// 上电自检：逐个舵机扫到低位 -> 高位 -> 回中位
#define SERVO_BOOT_SELF_TEST 1     // 1=上电自检，0=关闭
#define SERVO_TEST_ANGLE_LOW 45    // 自检低位角度
#define SERVO_TEST_ANGLE_HIGH 135  // 自检高位角度
#define SERVO_TEST_STEP_MS 400     // 每个角度停留时间（ms）

// 电池电量检测：电池 -> 200k -> GPIO1(ADC1_CH0) -> 100k -> GND
#define BATTERY_ADC_UNIT ADC_UNIT_1        // ADC 单元
#define BATTERY_ADC_CHANNEL ADC_CHANNEL_0  // ADC 通道（GPIO1）
#define BATTERY_UPPER_RESISTOR 200000.0f   // 上分压电阻（欧）
#define BATTERY_LOWER_RESISTOR 100000.0f   // 下分压电阻（欧）

// 电压标定系数：显示值 = 实测值 * 该系数。
// 当前台架实测：万用表 4.12V，固件显示 4.00V -> 4.12 / 4.00 = 1.03。
// 更换电阻或重新校准后按实际测量值调整。
#define BATTERY_VOLTAGE_SCALE 1.03f  // 电压标定系数

// TP4057 充电状态：CHRG 为开漏输出，充电时拉低
#define BATTERY_CHARGING_PIN GPIO_NUM_2   // TP4057 CHRG 检测脚
#define BATTERY_CHARGING_ACTIVE_LOW 1     // 1=低电平表示充电中，0=高电平表示充电中

// 低电量提醒阈值（百分比），低于该值且未充电时提示充电
#define BATTERY_LOW_LEVEL 15  // 低电量阈值（%）

// MPU6050 姿态传感器（I2C）
#define IMU_I2C_PORT 0             // I2C 端口
#define IMU_SDA_GPIO GPIO_NUM_4    // I2C SDA
#define IMU_SCL_GPIO GPIO_NUM_5    // I2C SCL
#define IMU_I2C_ADDR 0x68          // 7 位地址（AD0 接地 0x68，接 3.3V 0x69）
#define IMU_I2C_SPEED_HZ 400000    // I2C 时钟频率
// 50 Hz = 20 ms，对应上位机遥测帧率
#define IMU_SAMPLE_INTERVAL_MS 20  // 采样周期（ms）

// 上位机串口遥测（见 scripts/mpu6050/docs/serial-output-spec.md）
// 总开关在 config.h 顶部的 AI_SYSU_ENABLE_TELEMETRY
#define TELEMETRY_UART_NUM 0         // UART0（板载 CH340）
#define TELEMETRY_BAUD_RATE 115200   // 需与网页波特率一致
// 遥测开始后把日志级别降到 WARN，避免 INFO 日志混入 JSON 数据流
#define TELEMETRY_SILENCE_LOGS 1     // 1=遥测期间只输出 WARN 及以上日志

#endif // _BOARD_CONFIG_H_
