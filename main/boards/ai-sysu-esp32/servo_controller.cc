#include "servo_controller.h"

#include <algorithm>
#include <string>

#include <esp_log.h>

#include "config.h"
#include "mcp_server.h"

static const char* TAG = "ServoController";

namespace {
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kServoTimer = LEDC_TIMER_3;
constexpr ledc_timer_bit_t kPwmResolution = LEDC_TIMER_14_BIT;
constexpr uint32_t kPwmMaxDuty = (1 << 14) - 1;
constexpr int kPwmFrequencyHz = 50;
constexpr int kPwmPeriodUs = 1000000 / kPwmFrequencyHz;
constexpr int kMinAngle = 0;
constexpr int kMaxAngle = 180;
}  // namespace

ServoController::ServoController(const gpio_num_t* gpio_pins, const ledc_channel_t* channels,
                                 int count)
    : count_(std::clamp(count, 0, kMaxServos)) {
    mutex_ = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(mutex_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

    ledc_timer_config_t timer_config = {
        .speed_mode = kSpeedMode,
        .duty_resolution = kPwmResolution,
        .timer_num = kServoTimer,
        .freq_hz = kPwmFrequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    for (int i = 0; i < count_; ++i) {
        gpio_pins_[i] = gpio_pins[i];
        channels_[i] = channels[i];
        angles_[i] = SERVO_INIT_ANGLE;

        ledc_channel_config_t channel_config = {
            .gpio_num = gpio_pins_[i],
            .speed_mode = kSpeedMode,
            .channel = channels_[i],
            .timer_sel = kServoTimer,
            .duty = AngleToDuty(SERVO_INIT_ANGLE),
            .hpoint = 0,
        };
        ESP_ERROR_CHECK(ledc_channel_config(&channel_config));
        ledc_set_duty(kSpeedMode, channels_[i], AngleToDuty(SERVO_INIT_ANGLE));
        ledc_update_duty(kSpeedMode, channels_[i]);

        ESP_LOGI(TAG, "servo %d initialized (GPIO=%d, channel=%d, angle=%d)", i + 1,
                 static_cast<int>(gpio_pins_[i]), static_cast<int>(channels_[i]), SERVO_INIT_ANGLE);
    }

    RegisterMcpTools();
}

ServoController::~ServoController() {
    for (int i = 0; i < count_; ++i) {
        ledc_stop(kSpeedMode, channels_[i], 0);
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

uint32_t ServoController::AngleToDuty(int angle) const {
    angle = std::clamp(angle, kMinAngle, kMaxAngle);
    uint32_t pulse_us = SERVO_PWM_MIN_PULSE_US +
                        (SERVO_PWM_MAX_PULSE_US - SERVO_PWM_MIN_PULSE_US) * angle / kMaxAngle;
    return pulse_us * kPwmMaxDuty / kPwmPeriodUs;
}

void ServoController::SetAngle(int channel, int angle) {
    int index = channel - 1;
    if (index < 0 || index >= count_) {
        ESP_LOGW(TAG, "invalid servo channel %d (expected 1-%d)", channel, count_);
        return;
    }

    xSemaphoreTake(mutex_, portMAX_DELAY);
    SetAngleLocked(index, angle);
    xSemaphoreGive(mutex_);
}

void ServoController::SetAngleLocked(int index, int angle) {
    angle = std::clamp(angle, kMinAngle, kMaxAngle);
    angles_[index] = angle;
    ledc_set_duty(kSpeedMode, channels_[index], AngleToDuty(angle));
    ledc_update_duty(kSpeedMode, channels_[index]);
    ESP_LOGI(TAG, "servo %d: angle=%d", index + 1, angle);
}

int ServoController::GetAngle(int channel) const {
    int index = channel - 1;
    if (index < 0 || index >= count_) {
        return -1;
    }
    return angles_[index];
}

void ServoController::RegisterMcpTools() {
    if (count_ <= 0) {
        return;
    }
    auto& mcp_server = McpServer::GetInstance();

    mcp_server.AddTool("self.servo.set_angle",
                       "设置 SG90 舵机角度。channel 为舵机编号（1-" + std::to_string(count_) +
                           "），angle 为 0-180 度（90 为中位）。",
                       PropertyList({Property("channel", kPropertyTypeInteger, 1, 1, count_),
                                     Property("angle", kPropertyTypeInteger, 90, 0, 180)}),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int channel = properties["channel"].value<int>();
                           int angle = properties["angle"].value<int>();
                           SetAngle(channel, angle);
                           return true;
                       });

    mcp_server.AddTool("self.servo.get_angles", "获取所有舵机当前角度", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           std::string result = "{\"angles\":[";
                           for (int i = 0; i < count_; ++i) {
                               if (i > 0) {
                                   result += ",";
                               }
                               result += std::to_string(angles_[i]);
                           }
                           result += "]}";
                           return result;
                       });

    mcp_server.AddTool("self.servo.center_all", "将所有舵机恢复到中位（90 度）", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           for (int i = 0; i < count_; ++i) {
                               SetAngle(i + 1, SERVO_INIT_ANGLE);
                           }
                           return true;
                       });
}
