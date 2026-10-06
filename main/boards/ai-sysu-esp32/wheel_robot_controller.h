#ifndef WHEEL_ROBOT_CONTROLLER_H
#define WHEEL_ROBOT_CONTROLLER_H

#include <string>
#include <functional>
#include <atomic>
#include <mutex>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/gpio.h>
#include <driver/uart.h>

// 双轮足机器人串口控制器
// 协议参考同目录 README-串口控制.md：115200 8N1，行结束符 \n 或 \r\n
// 上电握手：收到小车 READY 后发送 HELLO，收到 OK 即握手成功（小车自动起立）。
class WheelRobotController {
public:
    WheelRobotController(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin);
    ~WheelRobotController();

    // 握手成功后回调一次；回调运行在接收任务上下文中，涉及 UI/音频的操作请自行 Schedule 到主任务
    void SetConnectedCallback(std::function<void()> callback);

    bool IsConnected() const { return connected_.load(); }

    // 运动控制。speed < 0 表示使用默认速度；duration_ms <= 0 表示持续运动（直到 STOP 或下一条运动指令）
    bool Forward(int speed = -1, int duration_ms = 0);
    bool Backward(int speed = -1, int duration_ms = 0);
    bool TurnLeft(int speed = -1, int duration_ms = 0);
    bool TurnRight(int speed = -1, int duration_ms = 0);
    bool Spin(const std::string& direction = "L", int duration_ms = 0);
    // 跳跃。direction 可取 F/B/L/R，为空表示原地跳
    bool Jump(const std::string& direction = "");
    bool Stop();
    bool Stand();
    bool Sit();
    bool MoveUp();
    bool MoveDown();
    bool SetHeight(int height);
    bool SetSpeed(int speed);

    // 返回小车最后一次报告的状态（"STANDING" / "SITTING" / 空字符串）
    std::string GetLastStatus() const;

private:
    void SendLine(const std::string& line);
    void HandleLine(const std::string& line);
    static void ReceiveTaskEntry(void* arg);
    void ReceiveTask();

    uart_port_t uart_num_;
    gpio_num_t tx_pin_;
    gpio_num_t rx_pin_;
    TaskHandle_t task_handle_ = nullptr;

    std::function<void()> connected_callback_;
    std::atomic<bool> connected_{false};
    bool waiting_ok_ = false;  // 已发送 HELLO，等待握手 OK
    mutable std::mutex status_mutex_;
    std::string last_status_;
};

#endif  // WHEEL_ROBOT_CONTROLLER_H
