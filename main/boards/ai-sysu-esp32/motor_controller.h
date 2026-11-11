#ifndef _MOTOR_CONTROLLER_H_
#define _MOTOR_CONTROLLER_H_

#include <driver/gpio.h>
#include <driver/ledc.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>

// DRV8833 单路直流电机驱动（一个通道占用 IN1/IN2 两路 PWM）。
//   IN1 PWM + IN2 低电平 -> 正转
//   IN2 PWM + IN1 低电平 -> 反转
//   两路均为低电平       -> 滑行停止
// nsleep_gpio 不为 GPIO_NUM_NC 时，初始化时会把 nSLEEP 拉高。
class MotorController {
public:
    MotorController(gpio_num_t in1_gpio, gpio_num_t in2_gpio, ledc_channel_t in1_channel,
                    ledc_channel_t in2_channel, const std::string& name,
                    gpio_num_t nsleep_gpio = GPIO_NUM_NC);
    ~MotorController();

    // speed_percent: -100 ~ 100，正数正转，负数反转，0 停止
    void SetSpeed(int speed_percent);
    // 旋转 duration_ms 毫秒后自动停止；duration_ms 为 0 时持续旋转
    void Rotate(int speed_percent, uint32_t duration_ms);
    void Stop();
    int GetSpeed() const { return speed_; }

private:
    void SetSpeedLocked(int speed_percent);
    void RegisterMcpTools();
    static void OnStopTimer(void* arg);

    gpio_num_t in1_gpio_;
    gpio_num_t in2_gpio_;
    ledc_channel_t in1_channel_;
    ledc_channel_t in2_channel_;
    std::string name_;
    esp_timer_handle_t stop_timer_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    int speed_ = 0;
};

#endif  // _MOTOR_CONTROLLER_H_
