#include "wifi_board.h"
#include "codecs/no_audio_codec.h"
#include "display/lcd_display.h"
#include "system_reset.h"
#include "application.h"
#include "button.h"
#include "config.h"
#include "assets/lang_config.h"
#include "mcp_server.h"
#include "lamp_controller.h"
#include "led/single_led.h"
#include "wheel_robot_controller.h"

#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/uart.h>
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

class AiSysuEsp32 : public WifiBoard {
private:

    Button boot_button_;
    LcdDisplay* display_;
    WheelRobotController robot_;

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
        display_ = new SpiLcdDisplay(panel_io, panel,
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
        auto& mcp_server = McpServer::GetInstance();

        mcp_server.AddTool("robot.forward",
                           "让双轮足机器人前进（往前走）。可指定速度(1-100)和持续时间(毫秒)，"
                           "不指定持续时间则一直前进直到停止。",
                           PropertyList({Property("speed", kPropertyTypeInteger, 60, 1, 100),
                                         Property("duration", kPropertyTypeInteger, 0, 0, 600000)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.Forward(properties["speed"].value<int>(),
                                              properties["duration"].value<int>());
                               return true;
                           });

        mcp_server.AddTool("robot.backward",
                           "让双轮足机器人后退。可指定速度(1-100)和持续时间(毫秒)。",
                           PropertyList({Property("speed", kPropertyTypeInteger, 60, 1, 100),
                                         Property("duration", kPropertyTypeInteger, 0, 0, 600000)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.Backward(properties["speed"].value<int>(),
                                               properties["duration"].value<int>());
                               return true;
                           });

        mcp_server.AddTool("robot.turn_left",
                           "让双轮足机器人原地左转。可指定速度(1-100)和持续时间(毫秒)。",
                           PropertyList({Property("speed", kPropertyTypeInteger, 60, 1, 100),
                                         Property("duration", kPropertyTypeInteger, 0, 0, 600000)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.TurnLeft(properties["speed"].value<int>(),
                                               properties["duration"].value<int>());
                               return true;
                           });

        mcp_server.AddTool("robot.turn_right",
                           "让双轮足机器人原地右转。可指定速度(1-100)和持续时间(毫秒)。",
                           PropertyList({Property("speed", kPropertyTypeInteger, 60, 1, 100),
                                         Property("duration", kPropertyTypeInteger, 0, 0, 600000)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.TurnRight(properties["speed"].value<int>(),
                                                properties["duration"].value<int>());
                               return true;
                           });

        mcp_server.AddTool("robot.spin",
                           "让双轮足机器人原地转圈。direction 为 L(左转一圈) 或 R(右转一圈)，"
                           "可指定持续时间(毫秒)。",
                           PropertyList({Property("direction", kPropertyTypeString, std::string("L")),
                                         Property("duration", kPropertyTypeInteger, 0, 0, 600000)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.Spin(properties["direction"].value<std::string>(),
                                           properties["duration"].value<int>());
                               return true;
                           });

        mcp_server.AddTool("robot.jump",
                           "让双轮足机器人跳跃。direction 只能取 F(向前)、B(向后)、L(向左)、R(向右)，"
                           "不填则原地跳。",
                           PropertyList({Property("direction", kPropertyTypeString, std::string(""))}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.Jump(properties["direction"].value<std::string>());
                               return true;
                           });

        mcp_server.AddTool("robot.stop", "让双轮足机器人停止所有运动（保持站立）。", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.Stop();
                               return true;
                           });

        mcp_server.AddTool("robot.stand", "让双轮足机器人起立（站起来）。", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.Stand();
                               return true;
                           });

        mcp_server.AddTool("robot.sit", "让双轮足机器人坐下（蹲下）。", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.Sit();
                               return true;
                           });

        mcp_server.AddTool("robot.move_up", "让双轮足机器人机身升高一档。", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.MoveUp();
                               return true;
                           });

        mcp_server.AddTool("robot.move_down", "让双轮足机器人机身降低一档。", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.MoveDown();
                               return true;
                           });

        mcp_server.AddTool("robot.set_height", "设置双轮足机器人机身高度(0-100)。",
                           PropertyList({Property("height", kPropertyTypeInteger, 0, 100)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.SetHeight(properties["height"].value<int>());
                               return true;
                           });

        mcp_server.AddTool("robot.set_speed", "设置双轮足机器人的默认运动速度档位(1-100)。",
                           PropertyList({Property("speed", kPropertyTypeInteger, 1, 100)}),
                           [this](const PropertyList& properties) -> ReturnValue {
                               robot_.SetSpeed(properties["speed"].value<int>());
                               return true;
                           });

        mcp_server.AddTool("robot.get_status", "查询双轮足机器人的连接与站立状态。", PropertyList(),
                           [this](const PropertyList& properties) -> ReturnValue {
                               if (!robot_.IsConnected()) {
                                   return std::string("未连接");
                               }
                               std::string status = robot_.GetLastStatus();
                               if (status == "STANDING") {
                                   return std::string("站立中");
                               }
                               if (status == "SITTING") {
                                   return std::string("坐着");
                               }
                               return std::string("已连接");
                           });
    }

public:
    AiSysuEsp32() :
        boot_button_(BOOT_BUTTON_GPIO),
        robot_(UART_NUM_1, ROBOT_UART_TX_PIN, ROBOT_UART_RX_PIN) {
        InitializeSpi();
        InitializeLcdDisplay();
        InitializeButtons();

        // 握手成功后语音提示（回调运行在串口接收任务中，UI/音频操作调度回主任务）
        robot_.SetConnectedCallback([]() {
            Application::GetInstance().Schedule([]() {
                Application::GetInstance().Alert("轮足机器人",
                                                 "已经链接上轮足机器人，您可以通过语音控制我运动",
                                                 "happy", Lang::Sounds::OGG_SUCCESS);
            });
        });

        InitializeTools();
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

    virtual Backlight* GetBacklight() override {
        if (DISPLAY_BACKLIGHT_PIN != GPIO_NUM_NC) {
            static PwmBacklight backlight(DISPLAY_BACKLIGHT_PIN, DISPLAY_BACKLIGHT_OUTPUT_INVERT);
            return &backlight;
        }
        return nullptr;
    }
};

DECLARE_BOARD(AiSysuEsp32);
