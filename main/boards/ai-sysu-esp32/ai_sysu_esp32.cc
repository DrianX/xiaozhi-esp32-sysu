#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "lvgl_theme.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#if AI_SYSU_ENABLE_MOTORS
#include "motor_controller.h"
#endif
#if AI_SYSU_ENABLE_SERVOS
#include "servo_controller.h"
#endif
#include "adc_battery_monitor.h"
#include "assets/lang_config.h"
#if AI_SYSU_ENABLE_CAMERA
#include "esp_video.h"
#endif
#if AI_SYSU_ENABLE_IMU
#include "mpu6050.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include <driver/ledc.h>
#include <driver/uart.h>
#include <esp_log.h>
#include <esp_system.h>
#include <driver/i2c_master.h>
#include <esp_lcd_panel_vendor.h>
#include <esp_lcd_panel_io.h>
#include <esp_lcd_panel_ops.h>
#include <driver/spi_common.h>

#if defined(LCD_TYPE_ILI9341_SERIAL)
#include "esp_lcd_ili9341.h"
#endif

#if defined(LCD_TYPE_GC9A01_SERIAL)
#include "esp_lcd_gc9a01.h"
static const gc9a01_lcd_init_cmd_t gc9107_lcd_init_cmds[] = {
    //  {cmd, { data }, data_size, delay_ms}
    {0xfe, (uint8_t[]){0x00}, 0, 0},
    {0xef, (uint8_t[]){0x00}, 0, 0},
    {0xb0, (uint8_t[]){0xc0}, 1, 0},
    {0xb1, (uint8_t[]){0x80}, 1, 0},
    {0xb2, (uint8_t[]){0x27}, 1, 0},
    {0xb3, (uint8_t[]){0x13}, 1, 0},
    {0xb6, (uint8_t[]){0x19}, 1, 0},
    {0xb7, (uint8_t[]){0x05}, 1, 0},
    {0xac, (uint8_t[]){0xc8}, 1, 0},
    {0xab, (uint8_t[]){0x0f}, 1, 0},
    {0x3a, (uint8_t[]){0x05}, 1, 0},
    {0xb4, (uint8_t[]){0x04}, 1, 0},
    {0xa8, (uint8_t[]){0x08}, 1, 0},
    {0xb8, (uint8_t[]){0x08}, 1, 0},
    {0xea, (uint8_t[]){0x02}, 1, 0},
    {0xe8, (uint8_t[]){0x2A}, 1, 0},
    {0xe9, (uint8_t[]){0x47}, 1, 0},
    {0xe7, (uint8_t[]){0x5f}, 1, 0},
    {0xc6, (uint8_t[]){0x21}, 1, 0},
    {0xc7, (uint8_t[]){0x15}, 1, 0},
    {0xf0,
    (uint8_t[]){0x1D, 0x38, 0x09, 0x4D, 0x92, 0x2F, 0x35, 0x52, 0x1E, 0x0C,
                0x04, 0x12, 0x14, 0x1f},
    14, 0},
    {0xf1,
    (uint8_t[]){0x16, 0x40, 0x1C, 0x54, 0xA9, 0x2D, 0x2E, 0x56, 0x10, 0x0D,
                0x0C, 0x1A, 0x14, 0x1E},
    14, 0},
    {0xf4, (uint8_t[]){0x00, 0x00, 0xFF}, 3, 0},
    {0xba, (uint8_t[]){0xFF, 0xFF}, 2, 0},
};
#endif

#define TAG "AiSysuEsp32"

#if AI_SYSU_ENABLE_IMU && AI_SYSU_ENABLE_TELEMETRY
// 用整数定点输出，避免 newlib-nano 的浮点 printf 问题
static void FormatFixed(char* out, size_t size, float value, int decimals) {
    long scale = (decimals == 3) ? 1000 : (decimals == 2) ? 100 : 10;
    long scaled = lroundf(value * scale);
    bool negative = scaled < 0;
    if (negative) {
        scaled = -scaled;
    }
    if (decimals == 3) {
        snprintf(out, size, "%s%ld.%03ld", negative ? "-" : "", scaled / 1000, scaled % 1000);
    } else if (decimals == 2) {
        snprintf(out, size, "%s%ld.%02ld", negative ? "-" : "", scaled / 100, scaled % 100);
    } else {
        snprintf(out, size, "%s%ld.%01ld", negative ? "-" : "", scaled / 10, scaled % 10);
    }
}

static void InitializeTelemetry() {
    ESP_ERROR_CHECK(
        uart_set_baudrate(static_cast<uart_port_t>(TELEMETRY_UART_NUM), TELEMETRY_BAUD_RATE));
#if TELEMETRY_SILENCE_LOGS
    // 上位机要求正式输出后不要混入日志
    esp_log_level_set("*", ESP_LOG_WARN);
#endif
}

static void SendImuTelemetry(const Mpu6050& imu) {
    float roll = imu.roll();
    float pitch = imu.pitch();
    float yaw = imu.yaw();
    float ax = imu.ax();
    float ay = imu.ay();
    float az = imu.az();
    float gx = imu.gx();
    float gy = imu.gy();
    float gz = imu.gz();
    float temp = imu.temperature();

    if (!std::isfinite(roll) || !std::isfinite(pitch) || !std::isfinite(yaw) ||
        !std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(gx) ||
        !std::isfinite(gy) || !std::isfinite(gz) || !std::isfinite(temp)) {
        return;  // 绝不发送 nan / inf
    }

    char roll_s[16];
    char pitch_s[16];
    char yaw_s[16];
    char ax_s[16];
    char ay_s[16];
    char az_s[16];
    char gx_s[16];
    char gy_s[16];
    char gz_s[16];
    char temp_s[16];
    FormatFixed(roll_s, sizeof(roll_s), roll, 2);
    FormatFixed(pitch_s, sizeof(pitch_s), pitch, 2);
    FormatFixed(yaw_s, sizeof(yaw_s), yaw, 2);
    FormatFixed(ax_s, sizeof(ax_s), ax, 3);
    FormatFixed(ay_s, sizeof(ay_s), ay, 3);
    FormatFixed(az_s, sizeof(az_s), az, 3);
    FormatFixed(gx_s, sizeof(gx_s), gx, 1);
    FormatFixed(gy_s, sizeof(gy_s), gy, 1);
    FormatFixed(gz_s, sizeof(gz_s), gz, 1);
    FormatFixed(temp_s, sizeof(temp_s), temp, 1);

    char line[256];
    int len = snprintf(line, sizeof(line),
                       "{\"roll\":%s,\"pitch\":%s,\"yaw\":%s,"
                       "\"ax\":%s,\"ay\":%s,\"az\":%s,"
                       "\"gx\":%s,\"gy\":%s,\"gz\":%s,\"temp\":%s}\n",
                       roll_s, pitch_s, yaw_s, ax_s, ay_s, az_s, gx_s, gy_s, gz_s, temp_s);
    if (len > 0 && len < static_cast<int>(sizeof(line))) {
        // 控制台 UART 没有通过 uart_driver_install 安装，uart_write_bytes 会报
        // "uart driver error"；走 stdio/控制台路径即可。
        fwrite(line, 1, len, stdout);
        fflush(stdout);
    }
}
#endif  // AI_SYSU_ENABLE_IMU && AI_SYSU_ENABLE_TELEMETRY

#if (AI_SYSU_ENABLE_MOTORS && MOTOR_BOOT_SELF_TEST) || (AI_SYSU_ENABLE_SERVOS && SERVO_BOOT_SELF_TEST)
struct SelfTestContext {
#if AI_SYSU_ENABLE_MOTORS
    MotorController* motor_a = nullptr;
    MotorController* motor_b = nullptr;
#endif
#if AI_SYSU_ENABLE_SERVOS
    ServoController* servos = nullptr;
#endif
};

static SelfTestContext self_test_ctx;

#if AI_SYSU_ENABLE_MOTORS && MOTOR_BOOT_SELF_TEST
static void RunMotorTest(MotorController* motor, const char* label) {
    if (motor == nullptr) {
        return;
    }
    ESP_LOGI(TAG, "%s self-test: forward %d%%", label, MOTOR_TEST_SPEED);
    motor->SetSpeed(MOTOR_TEST_SPEED);
    vTaskDelay(pdMS_TO_TICKS(MOTOR_TEST_DURATION_MS));

    ESP_LOGI(TAG, "%s self-test: reverse %d%%", label, MOTOR_TEST_SPEED);
    motor->SetSpeed(-MOTOR_TEST_SPEED);
    vTaskDelay(pdMS_TO_TICKS(MOTOR_TEST_DURATION_MS));

    motor->Stop();
    ESP_LOGI(TAG, "%s self-test: done", label);
}
#endif  // AI_SYSU_ENABLE_MOTORS && MOTOR_BOOT_SELF_TEST

static void HardwareSelfTestTask(void* arg) {
    auto* ctx = static_cast<SelfTestContext*>(arg);
    vTaskDelay(pdMS_TO_TICKS(1000));

#if AI_SYSU_ENABLE_MOTORS && MOTOR_BOOT_SELF_TEST
    RunMotorTest(ctx->motor_a, "Motor A");
    RunMotorTest(ctx->motor_b, "Motor B");
#endif

#if AI_SYSU_ENABLE_SERVOS && SERVO_BOOT_SELF_TEST
    if (ctx->servos != nullptr) {
        for (int channel = 1; channel <= ctx->servos->GetCount(); ++channel) {
            ESP_LOGI(TAG, "Servo %d self-test", channel);
            ctx->servos->SetAngle(channel, SERVO_TEST_ANGLE_LOW);
            vTaskDelay(pdMS_TO_TICKS(SERVO_TEST_STEP_MS));
            ctx->servos->SetAngle(channel, SERVO_TEST_ANGLE_HIGH);
            vTaskDelay(pdMS_TO_TICKS(SERVO_TEST_STEP_MS));
            ctx->servos->SetAngle(channel, SERVO_INIT_ANGLE);
            vTaskDelay(pdMS_TO_TICKS(SERVO_TEST_STEP_MS));
        }
    }
#endif

    ESP_LOGI(TAG, "Hardware self-test: done");
    vTaskDelete(nullptr);
}
#endif  // (AI_SYSU_ENABLE_MOTORS && MOTOR_BOOT_SELF_TEST) || (AI_SYSU_ENABLE_SERVOS && SERVO_BOOT_SELF_TEST)

// 充电时把右上角电池图标显示为绿色
class AiSysuLcdDisplay : public SpiLcdDisplay {
public:
    AiSysuLcdDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy)
        : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                        swap_xy) {}

    void UpdateStatusBar(bool update_all = false) override {
        SpiLcdDisplay::UpdateStatusBar(update_all);

        int level = 0;
        bool charging = false;
        bool discharging = false;
        if (!Board::GetInstance().GetBatteryLevel(level, charging, discharging)) {
            return;
        }

        auto* theme = static_cast<LvglTheme*>(current_theme_);
        DisplayLockGuard lock(this);
        if (battery_label_ == nullptr) {
            return;
        }
        if (charging) {
            lv_obj_set_style_text_color(battery_label_, lv_color_hex(0x00C853), 0);
        } else if (theme != nullptr) {
            lv_obj_set_style_text_color(battery_label_, theme->text_color(), 0);
        }
    }

    void SetPreviewImage(std::unique_ptr<LvglImage> image) override {
        bool has_image = image != nullptr;
        LcdDisplay::SetPreviewImage(std::move(image));
        if (!has_image) {
            return;
        }

        DisplayLockGuard lock(this);
        if (preview_image_ == nullptr || preview_image_cached_ == nullptr) {
            return;
        }

        auto* img_dsc = preview_image_cached_->image_dsc();
        if (img_dsc == nullptr || img_dsc->header.w == 0 || img_dsc->header.h == 0) {
            return;
        }

        // 放大到屏幕的 90%，保持宽高比
        uint32_t target_w = width_ * 9 / 10;
        uint32_t target_h = height_ * 9 / 10;
        uint32_t scale = std::min(256u * target_w / img_dsc->header.w,
                                  256u * target_h / img_dsc->header.h);
        lv_obj_set_size(preview_image_, width_, height_);
        lv_image_set_scale(preview_image_, scale);
    }

};

class AiSysuEsp32 : public WifiBoard {
private:

    Button boot_button_;
    LcdDisplay* display_;
#if AI_SYSU_ENABLE_MOTORS
    MotorController* motor_a_ = nullptr;
    MotorController* motor_b_ = nullptr;
#endif
#if AI_SYSU_ENABLE_SERVOS
    ServoController* servos_ = nullptr;
#endif
    AdcBatteryMonitor* battery_monitor_ = nullptr;
    bool low_battery_notified_ = false;
#if AI_SYSU_ENABLE_CAMERA
    EspVideo* camera_ = nullptr;
#endif
#if AI_SYSU_ENABLE_IMU
    Mpu6050* imu_ = nullptr;
#endif

    void InitializeSpi() {
        spi_bus_config_t buscfg = {};
        buscfg.mosi_io_num = DISPLAY_MOSI_PIN;
        buscfg.miso_io_num = GPIO_NUM_NC;
        buscfg.sclk_io_num = DISPLAY_CLK_PIN;
        buscfg.quadwp_io_num = GPIO_NUM_NC;
        buscfg.quadhd_io_num = GPIO_NUM_NC;
        buscfg.max_transfer_sz = DISPLAY_WIDTH * DISPLAY_HEIGHT * sizeof(uint16_t);
        ESP_ERROR_CHECK(spi_bus_initialize(SPI3_HOST, &buscfg, SPI_DMA_CH_AUTO));
    }

    void InitializeLcdDisplay() {
        esp_lcd_panel_io_handle_t panel_io = nullptr;
        esp_lcd_panel_handle_t panel = nullptr;
        // 液晶屏控制IO初始化
        ESP_LOGD(TAG, "Install panel IO");
        esp_lcd_panel_io_spi_config_t io_config = {};
        io_config.cs_gpio_num = DISPLAY_CS_PIN;
        io_config.dc_gpio_num = DISPLAY_DC_PIN;
        io_config.spi_mode = DISPLAY_SPI_MODE;
        io_config.pclk_hz = 40 * 1000 * 1000;
        io_config.trans_queue_depth = 10;
        io_config.lcd_cmd_bits = 8;
        io_config.lcd_param_bits = 8;
        ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(SPI3_HOST, &io_config, &panel_io));

        // 初始化液晶屏驱动芯片
        ESP_LOGD(TAG, "Install LCD driver");
        esp_lcd_panel_dev_config_t panel_config = {};
        panel_config.reset_gpio_num = DISPLAY_RST_PIN;
        panel_config.rgb_ele_order = DISPLAY_RGB_ORDER;
        panel_config.bits_per_pixel = 16;
#if defined(LCD_TYPE_ILI9341_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_ili9341(panel_io, &panel_config, &panel));
#elif defined(LCD_TYPE_GC9A01_SERIAL)
        ESP_ERROR_CHECK(esp_lcd_new_panel_gc9a01(panel_io, &panel_config, &panel));
        gc9a01_vendor_config_t gc9107_vendor_config = {
            .init_cmds = gc9107_lcd_init_cmds,
            .init_cmds_size = sizeof(gc9107_lcd_init_cmds) / sizeof(gc9a01_lcd_init_cmd_t),
        };
#else
        ESP_ERROR_CHECK(esp_lcd_new_panel_st7789(panel_io, &panel_config, &panel));
#endif

        esp_lcd_panel_reset(panel);

        esp_lcd_panel_init(panel);
        esp_lcd_panel_invert_color(panel, DISPLAY_INVERT_COLOR);
        esp_lcd_panel_swap_xy(panel, DISPLAY_SWAP_XY);
        esp_lcd_panel_mirror(panel, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y);
#ifdef  LCD_TYPE_GC9A01_SERIAL
        panel_config.vendor_config = &gc9107_vendor_config;
#endif
        display_ = new AiSysuLcdDisplay(panel_io, panel,
                                    DISPLAY_WIDTH, DISPLAY_HEIGHT, DISPLAY_OFFSET_X, DISPLAY_OFFSET_Y, DISPLAY_MIRROR_X, DISPLAY_MIRROR_Y, DISPLAY_SWAP_XY);
    }

    void InitializeButtons() {
        boot_button_.OnClick([this]() {
            auto& app = Application::GetInstance();
            if (app.GetDeviceState() == kDeviceStateStarting) {
                EnterWifiConfigMode();
                return;
            }
            app.ToggleChatState();
        });
    }

    // 物联网初始化，添加对 AI 可见设备
    void InitializeTools() {
        static LampController lamp(LAMP_GPIO);
    }

#if AI_SYSU_ENABLE_MOTORS
    void InitializeMotor() {
        motor_a_ = new MotorController(MOTOR_A_IN1_GPIO, MOTOR_A_IN2_GPIO, LEDC_CHANNEL_2,
                                       LEDC_CHANNEL_3, "motor_a", MOTOR_A_NSLEEP_GPIO);
        motor_b_ = new MotorController(MOTOR_B_IN1_GPIO, MOTOR_B_IN2_GPIO, LEDC_CHANNEL_4,
                                       LEDC_CHANNEL_5, "motor_b", MOTOR_B_NSLEEP_GPIO);
    }
#endif

#if AI_SYSU_ENABLE_SERVOS
    void InitializeServo() {
        static const gpio_num_t servo_gpios[SERVO_COUNT] = {SERVO_1_GPIO, SERVO_2_GPIO,
                                                            SERVO_3_GPIO, SERVO_4_GPIO};
        // 通道 0/1/6/7 留给舵机；本板未使用背光（背光默认占用通道 0）
        static const ledc_channel_t servo_channels[SERVO_COUNT] = {
            LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_6, LEDC_CHANNEL_7};
        servos_ = new ServoController(servo_gpios, servo_channels, SERVO_COUNT);
    }
#endif

    void InitializeSelfTest() {
#if (AI_SYSU_ENABLE_MOTORS && MOTOR_BOOT_SELF_TEST) || (AI_SYSU_ENABLE_SERVOS && SERVO_BOOT_SELF_TEST)
#if AI_SYSU_ENABLE_MOTORS
        self_test_ctx.motor_a = motor_a_;
        self_test_ctx.motor_b = motor_b_;
#endif
#if AI_SYSU_ENABLE_SERVOS
        self_test_ctx.servos = servos_;
#endif
        xTaskCreate(HardwareSelfTestTask, "hw_self_test", 4096, &self_test_ctx,
                    tskIDLE_PRIORITY + 2, nullptr);
#endif
    }

    void InitializeBattery() {
        battery_monitor_ =
            new AdcBatteryMonitor(BATTERY_ADC_UNIT, BATTERY_ADC_CHANNEL, BATTERY_UPPER_RESISTOR,
                                  BATTERY_LOWER_RESISTOR, BATTERY_CHARGING_PIN,
                                  BATTERY_CHARGING_ACTIVE_LOW, BATTERY_VOLTAGE_SCALE);
    }

#if AI_SYSU_ENABLE_CAMERA && defined(CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE)
    void InitializeCamera() {
        // ESP32-S3 原生 USB（D- = GPIO19, D+ = GPIO20），USB Host 由 esp_video 初始化
        esp_video_init_usb_uvc_config_t usb_uvc_config = {
            .uvc = {
                .uvc_dev_num = 1,
                .task_stack = 4096,
                .task_priority = 5,
                .task_affinity = -1,
            },
            .usb = {
                .init_usb_host_lib = true,
                .task_stack = 4096,
                .task_priority = 5,
                .task_affinity = -1,
            },
        };

        esp_video_init_config_t video_config = {
            .usb_uvc = &usb_uvc_config,
        };

        camera_ = new EspVideo(video_config);
    }
#endif

#if AI_SYSU_ENABLE_IMU
    void InitializeIMU() {
        imu_ = new Mpu6050(IMU_I2C_PORT, IMU_SDA_GPIO, IMU_SCL_GPIO, IMU_I2C_ADDR,
                           IMU_I2C_SPEED_HZ);
        if (!imu_->Init()) {
            ESP_LOGE(TAG, "MPU6050 init failed");
            delete imu_;
            imu_ = nullptr;
            return;
        }
        xTaskCreate(ImuTaskEntry, "imu_task", 6144, this, tskIDLE_PRIORITY + 3, nullptr);
    }

    static void ImuTaskEntry(void* arg) {
        static_cast<AiSysuEsp32*>(arg)->ImuLoop();
    }

    void ImuLoop() {
        TickType_t last_wake = xTaskGetTickCount();
        const TickType_t period = pdMS_TO_TICKS(IMU_SAMPLE_INTERVAL_MS);
        while (true) {
            if (imu_ != nullptr && imu_->Update()) {
#if AI_SYSU_ENABLE_TELEMETRY
                // 50 Hz 串口遥测
                SendImuTelemetry(*imu_);
#endif
            }
            vTaskDelayUntil(&last_wake, period);
        }
    }
#endif  // AI_SYSU_ENABLE_IMU

public:
    AiSysuEsp32() :
        boot_button_(BOOT_BUTTON_GPIO) {
        ESP_LOGI(TAG, "Reset reason: %d", static_cast<int>(esp_reset_reason()));
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();
        InitializeTools();
#if AI_SYSU_ENABLE_MOTORS
        InitializeMotor();
#endif
#if AI_SYSU_ENABLE_SERVOS
        InitializeServo();
#endif
        InitializeBattery();
#if AI_SYSU_ENABLE_CAMERA && defined(CONFIG_ESP_VIDEO_ENABLE_USB_UVC_VIDEO_DEVICE)
        InitializeCamera();
#endif
#if AI_SYSU_ENABLE_IMU && AI_SYSU_ENABLE_TELEMETRY
        InitializeTelemetry();
#endif
#if AI_SYSU_ENABLE_IMU
        InitializeIMU();
#endif
        InitializeSelfTest();
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            GetBacklight()->RestoreBrightness();
        }

    }

    virtual Led* GetLed() override {
        static SingleLed led(BUILTIN_LED_GPIO);
        return &led;
    }

    virtual AudioCodec* GetAudioCodec() override {
#ifdef AUDIO_I2S_METHOD_SIMPLEX
        static NoAudioCodecSimplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_SPK_GPIO_BCLK, AUDIO_I2S_SPK_GPIO_LRCK, AUDIO_I2S_SPK_GPIO_DOUT, AUDIO_I2S_MIC_GPIO_SCK, AUDIO_I2S_MIC_GPIO_WS, AUDIO_I2S_MIC_GPIO_DIN);
#else
        static NoAudioCodecDuplex audio_codec(AUDIO_INPUT_SAMPLE_RATE, AUDIO_OUTPUT_SAMPLE_RATE,
            AUDIO_I2S_GPIO_BCLK, AUDIO_I2S_GPIO_WS, AUDIO_I2S_GPIO_DOUT, AUDIO_I2S_GPIO_DIN);
#endif
        return &audio_codec;
    }

    virtual Display* GetDisplay() override {
        return display_;
    }

    virtual Camera* GetCamera() override {
#if AI_SYSU_ENABLE_CAMERA
        return camera_;
#else
        return nullptr;
#endif
    }

    virtual bool GetBatteryLevel(int& level, bool& charging, bool& discharging) override {
        if (battery_monitor_ == nullptr) {
            return false;
        }

        charging = battery_monitor_->IsCharging();
        discharging = battery_monitor_->IsDischarging();
        level = battery_monitor_->GetBatteryLevel();

        // 低电量且未充电时提示一次；恢复充电或电量回升后重新布防
        bool low_battery = discharging && level <= BATTERY_LOW_LEVEL;
        if (low_battery && !low_battery_notified_) {
            low_battery_notified_ = true;
            if (auto display = GetDisplay(); display != nullptr) {
                display->ShowNotification(Lang::Strings::BATTERY_NEED_CHARGE, 5000);
            }
        } else if (!low_battery) {
            low_battery_notified_ = false;
        }
        return true;
    }

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(AiSysuEsp32);
