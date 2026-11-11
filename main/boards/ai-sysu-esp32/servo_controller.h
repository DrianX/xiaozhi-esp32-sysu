#ifndef _SERVO_CONTROLLER_H_
#define _SERVO_CONTROLLER_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// SG90 等 50 Hz 舵机驱动。
// 每个舵机占用一路 LEDC PWM，脉宽范围由 config.h 中的
// SERVO_PWM_MIN_PULSE_US / SERVO_PWM_MAX_PULSE_US 决定。
class ServoController {
public:
    ServoController(const gpio_num_t* gpio_pins, const ledc_channel_t* channels, int count);
    ~ServoController();

    int GetCount() const { return count_; }
    // channel 从 1 开始
    void SetAngle(int channel, int angle);
    int GetAngle(int channel) const;

private:
    static constexpr int kMaxServos = 4;

    uint32_t AngleToDuty(int angle) const;
    void SetAngleLocked(int index, int angle);
    void RegisterMcpTools();

    gpio_num_t gpio_pins_[kMaxServos] = {};
    ledc_channel_t channels_[kMaxServos] = {};
    int angles_[kMaxServos] = {};
    int count_ = 0;
    SemaphoreHandle_t mutex_ = nullptr;
};

#endif  // _SERVO_CONTROLLER_H_
