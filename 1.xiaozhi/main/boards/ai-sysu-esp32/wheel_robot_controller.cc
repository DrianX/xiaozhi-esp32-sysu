#include "wheel_robot_controller.h"

#include <esp_log.h>
#include <algorithm>
#include <cctype>

#define TAG "WheelRobot"

static constexpr int kUartBaudRate = 115200;
static constexpr int kRxBufferSize = 1024;
static constexpr int kReadChunk = 256;

namespace {
// 按 README 组装运动参数：
// 两个参数 -> "速度 时长"；单参数 <=100 视为速度、>100 视为时长；无参数 -> 默认速度持续运动。
void AppendMotionArgs(std::string& cmd, int speed, int duration_ms) {
    if (speed >= 0 && duration_ms > 0) {
        cmd += " " + std::to_string(speed) + " " + std::to_string(duration_ms);
    } else if (speed >= 0) {
        cmd += " " + std::to_string(speed);
    } else if (duration_ms > 0) {
        cmd += " " + std::to_string(duration_ms);
    }
}
}  // namespace

WheelRobotController::WheelRobotController(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin)
    : uart_num_(uart_num), tx_pin_(tx_pin), rx_pin_(rx_pin) {
    uart_config_t uart_config = {
        .baud_rate = kUartBaudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_ERROR_CHECK(uart_driver_install(uart_num_, kRxBufferSize * 2, 0, 0, nullptr, 0));
    ESP_ERROR_CHECK(uart_param_config(uart_num_, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(uart_num_, tx_pin_, rx_pin_, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    xTaskCreate(ReceiveTaskEntry, "wheel_robot", 4096, this, 5, &task_handle_);
}

WheelRobotController::~WheelRobotController() {
    if (task_handle_ != nullptr) {
        vTaskDelete(task_handle_);
    }
    uart_driver_delete(uart_num_);
}

void WheelRobotController::SetConnectedCallback(std::function<void()> callback) {
    connected_callback_ = std::move(callback);
}

void WheelRobotController::SendLine(const std::string& line) {
    std::string out = line + "\n";
    int written = uart_write_bytes(uart_num_, out.c_str(), out.size());
    if (written < 0) {
        ESP_LOGE(TAG, "uart_write_bytes failed");
    } else {
        ESP_LOGI(TAG, "TX: %s", line.c_str());
    }
}

void WheelRobotController::HandleLine(const std::string& line) {
    ESP_LOGI(TAG, "RX: %s", line.c_str());

    if (line == "READY") {
        // 小车就绪，发送 HELLO 握手并等待 OK（小车会自动起立）
        SendLine("HELLO");
        waiting_ok_ = true;
    } else if (line == "OK") {
        bool was_waiting = waiting_ok_;
        waiting_ok_ = false;
        if (was_waiting && !connected_.load()) {
            connected_.store(true);
            ESP_LOGI(TAG, "Robot handshake completed");
            if (connected_callback_) {
                connected_callback_();
            }
        }
    } else if (line == "STANDING" || line == "SITTING") {
        std::lock_guard<std::mutex> lock(status_mutex_);
        last_status_ = line;
    } else if (line == "ERR") {
        ESP_LOGW(TAG, "Robot rejected previous command (ERR)");
    } else {
        ESP_LOGD(TAG, "Unrecognized line: %s", line.c_str());
    }
}

void WheelRobotController::ReceiveTaskEntry(void* arg) {
    static_cast<WheelRobotController*>(arg)->ReceiveTask();
}

void WheelRobotController::ReceiveTask() {
    std::string buffer;
    uint8_t data[kReadChunk];
    while (true) {
        int len = uart_read_bytes(uart_num_, data, sizeof(data), pdMS_TO_TICKS(100));
        if (len > 0) {
            buffer.append(reinterpret_cast<const char*>(data), len);
            size_t pos;
            while ((pos = buffer.find('\n')) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                buffer.erase(0, pos + 1);
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                if (!line.empty()) {
                    HandleLine(line);
                }
            }
            // 防止缺少换行符的异常输入导致 buffer 无限增长
            if (buffer.size() > kRxBufferSize) {
                buffer.clear();
            }
        }
    }
}

bool WheelRobotController::Forward(int speed, int duration_ms) {
    std::string cmd = "FORWARD";
    AppendMotionArgs(cmd, speed, duration_ms);
    SendLine(cmd);
    return true;
}

bool WheelRobotController::Backward(int speed, int duration_ms) {
    std::string cmd = "BACK";
    AppendMotionArgs(cmd, speed, duration_ms);
    SendLine(cmd);
    return true;
}

bool WheelRobotController::TurnLeft(int speed, int duration_ms) {
    std::string cmd = "LEFT";
    AppendMotionArgs(cmd, speed, duration_ms);
    SendLine(cmd);
    return true;
}

bool WheelRobotController::TurnRight(int speed, int duration_ms) {
    std::string cmd = "RIGHT";
    AppendMotionArgs(cmd, speed, duration_ms);
    SendLine(cmd);
    return true;
}

bool WheelRobotController::Spin(const std::string& direction, int duration_ms) {
    std::string cmd = "SPIN";
    if (!direction.empty()) {
        char dir = static_cast<char>(std::toupper(static_cast<unsigned char>(direction[0])));
        if (dir == 'L' || dir == 'R') {
            cmd += " ";
            cmd += dir;
        }
    }
    if (duration_ms > 0) {
        cmd += " " + std::to_string(duration_ms);
    }
    SendLine(cmd);
    return true;
}

bool WheelRobotController::Jump(const std::string& direction) {
    std::string cmd = "JUMP";
    if (!direction.empty()) {
        char dir = static_cast<char>(std::toupper(static_cast<unsigned char>(direction[0])));
        if (dir == 'F' || dir == 'B' || dir == 'L' || dir == 'R') {
            cmd += " ";
            cmd += dir;
        }
    }
    SendLine(cmd);
    return true;
}

bool WheelRobotController::Stop() {
    SendLine("STOP");
    return true;
}

bool WheelRobotController::Stand() {
    SendLine("STAND");
    return true;
}

bool WheelRobotController::Sit() {
    SendLine("SIT");
    return true;
}

bool WheelRobotController::MoveUp() {
    SendLine("UP");
    return true;
}

bool WheelRobotController::MoveDown() {
    SendLine("DOWN");
    return true;
}

bool WheelRobotController::SetHeight(int height) {
    height = std::clamp(height, 0, 100);
    SendLine("HEIGHT " + std::to_string(height));
    return true;
}

bool WheelRobotController::SetSpeed(int speed) {
    speed = std::clamp(speed, 1, 100);
    SendLine("SPEED " + std::to_string(speed));
    return true;
}

std::string WheelRobotController::GetLastStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return last_status_;
}
