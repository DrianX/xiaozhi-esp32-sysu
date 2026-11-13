#include "config.h"

#if AI_SYSU_ENABLE_MOTORS

#include "motor_controller.h"

#include <algorithm>
#include <cinttypes>
#include <cstdlib>

#include <driver/ledc.h>
#include <esp_log.h>

#include "mcp_server.h"

static const char* TAG = "MotorController";

namespace {
constexpr ledc_mode_t kSpeedMode = LEDC_LOW_SPEED_MODE;
constexpr ledc_timer_t kPwmTimer = LEDC_TIMER_2;  // 两个电机通道共用 20 kHz 定时器
constexpr ledc_timer_bit_t kPwmResolution = LEDC_TIMER_10_BIT;
constexpr uint32_t kPwmFrequencyHz = 20000;
constexpr uint32_t kPwmMaxDuty = (1 << 10) - 1;
}  // namespace

MotorController::MotorController(gpio_num_t in1_gpio, gpio_num_t in2_gpio,
                                 ledc_channel_t in1_channel, ledc_channel_t in2_channel,
                                 const std::string& name, gpio_num_t nsleep_gpio)
    : in1_gpio_(in1_gpio),
      in2_gpio_(in2_gpio),
      in1_channel_(in1_channel),
      in2_channel_(in2_channel),
      name_(name) {
    mutex_ = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(mutex_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

    // DRV8833 的 nSLEEP 悬空时芯片处于休眠，输出被关闭
    if (nsleep_gpio != GPIO_NUM_NC) {
        gpio_config_t nsleep_config = {
            .pin_bit_mask = (1ULL << nsleep_gpio),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&nsleep_config));
        gpio_set_level(nsleep_gpio, 1);
    }

    ledc_timer_config_t timer_config = {
        .speed_mode = kSpeedMode,
        .duty_resolution = kPwmResolution,
        .timer_num = kPwmTimer,
        .freq_hz = kPwmFrequencyHz,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_ERROR_CHECK(ledc_timer_config(&timer_config));

    ledc_channel_config_t in1_config = {
        .gpio_num = in1_gpio_,
        .speed_mode = kSpeedMode,
        .channel = in1_channel_,
        .timer_sel = kPwmTimer,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&in1_config));

    ledc_channel_config_t in2_config = {
        .gpio_num = in2_gpio_,
        .speed_mode = kSpeedMode,
        .channel = in2_channel_,
        .timer_sel = kPwmTimer,
        .duty = 0,
        .hpoint = 0,
    };
    ESP_ERROR_CHECK(ledc_channel_config(&in2_config));

    const esp_timer_create_args_t timer_args = {
        .callback = OnStopTimer,
        .arg = this,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "motor_stop",
        .skip_unhandled_events = true,
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &stop_timer_));

    RegisterMcpTools();
    ESP_LOGI(TAG, "%s initialized (IN1=%d, IN2=%d, channels=%d/%d)", name_.c_str(),
             static_cast<int>(in1_gpio_), static_cast<int>(in2_gpio_),
             static_cast<int>(in1_channel_), static_cast<int>(in2_channel_));
}

MotorController::~MotorController() {
    Stop();
    if (stop_timer_ != nullptr) {
        esp_timer_delete(stop_timer_);
    }
    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

void MotorController::SetSpeed(int speed_percent) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    SetSpeedLocked(speed_percent);
    ESP_LOGI(TAG, "%s: speed=%d%%", name_.c_str(), speed_percent);
    xSemaphoreGive(mutex_);
}

void MotorController::SetSpeedLocked(int speed_percent) {
    speed_percent = std::clamp(speed_percent, -100, 100);
    speed_ = speed_percent;

    uint32_t duty = static_cast<uint32_t>(std::abs(speed_percent)) * kPwmMaxDuty / 100;
    ledc_set_duty(kSpeedMode, in1_channel_, speed_percent > 0 ? duty : 0);
    ledc_update_duty(kSpeedMode, in1_channel_);
    ledc_set_duty(kSpeedMode, in2_channel_, speed_percent < 0 ? duty : 0);
    ledc_update_duty(kSpeedMode, in2_channel_);
}

void MotorController::Rotate(int speed_percent, uint32_t duration_ms) {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    esp_timer_stop(stop_timer_);
    SetSpeedLocked(speed_percent);
    if (duration_ms > 0) {
        esp_err_t err =
            esp_timer_start_once(stop_timer_, static_cast<uint64_t>(duration_ms) * 1000);
        if (err != ESP_OK) {
            // 定时器异常时宁可不转，避免电机一直旋转
            ESP_LOGE(TAG, "Failed to start stop timer: %s", esp_err_to_name(err));
            SetSpeedLocked(0);
        }
    }
    xSemaphoreGive(mutex_);
    ESP_LOGI(TAG, "%s: rotate speed=%d, duration=%" PRIu32 " ms", name_.c_str(), speed_percent,
             duration_ms);
}

void MotorController::Stop() {
    xSemaphoreTake(mutex_, portMAX_DELAY);
    esp_timer_stop(stop_timer_);
    SetSpeedLocked(0);
    xSemaphoreGive(mutex_);
}

void MotorController::OnStopTimer(void* arg) {
    auto* controller = static_cast<MotorController*>(arg);
    xSemaphoreTake(controller->mutex_, portMAX_DELAY);
    controller->SetSpeedLocked(0);
    xSemaphoreGive(controller->mutex_);
    ESP_LOGI(TAG, "%s: rotate finished, motor stopped", controller->name_.c_str());
}

void MotorController::RegisterMcpTools() {
    auto& mcp_server = McpServer::GetInstance();
    const std::string prefix = "self." + name_;

    mcp_server.AddTool(
        prefix + ".spin",
        "控制" + name_ +
            "电机旋转。speed 为 -100~100 的整数：正数正转，负数反转，绝对值表示速度百分比；"
            "duration_ms 为旋转时长（毫秒），0 表示持续旋转直到调用 " +
            prefix + ".stop。",
        PropertyList({Property("speed", kPropertyTypeInteger, 60, -100, 100),
                      Property("duration_ms", kPropertyTypeInteger, 2000, 0, 60000)}),
        [this](const PropertyList& properties) -> ReturnValue {
            int speed = properties["speed"].value<int>();
            int duration_ms = properties["duration_ms"].value<int>();
            Rotate(speed, static_cast<uint32_t>(duration_ms));
            return true;
        });

    mcp_server.AddTool(prefix + ".stop", "立即停止" + name_ + "电机", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           Stop();
                           return true;
                       });

    mcp_server.AddTool(prefix + ".get_state",
                       "获取" + name_ + "电机当前状态（速度百分比，0 表示停止）", PropertyList(),
                       [this](const PropertyList& properties) -> ReturnValue {
                           int speed = GetSpeed();
                           const char* direction = "stopped";
                           if (speed > 0) {
                               direction = "forward";
                           } else if (speed < 0) {
                               direction = "reverse";
                           }
                           return std::string("{\"speed\":") + std::to_string(speed) +
                                  ",\"direction\":\"" + direction + "\"}";
                       });
}

#endif  // AI_SYSU_ENABLE_MOTORS
