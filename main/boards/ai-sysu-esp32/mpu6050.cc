#include "config.h"

#if AI_SYSU_ENABLE_IMU

#include "mpu6050.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "MPU6050";

namespace {
constexpr uint8_t kRegSmplrtDiv = 0x19;
constexpr uint8_t kRegConfig = 0x1A;
constexpr uint8_t kRegGyroConfig = 0x1B;
constexpr uint8_t kRegAccelConfig = 0x1C;
constexpr uint8_t kRegAccelXoutH = 0x3B;  // 0x3B..0x48: accel/temp/gyro
constexpr uint8_t kRegPwrMgmt1 = 0x6B;
constexpr uint8_t kRegWhoAmI = 0x75;

constexpr float kAccelScale = 16384.0f;  // ±2g
constexpr float kGyroScale = 65.5f;      // ±500 °/s
constexpr float kRadToDeg = 57.2957795f;
constexpr float kDegToRad = 0.0174532925f;
constexpr float kMahonyKp = 1.0f;
constexpr float kMahonyKi = 0.02f;
constexpr float kMaxIntegralFb = 10.0f;  // °/s，限制积分饱和
constexpr int kI2cTimeoutMs = 100;
constexpr int kGyroCalibrationSamples = 500;
}  // namespace

Mpu6050::Mpu6050(i2c_port_num_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio,
                 uint8_t address, uint32_t speed_hz)
    : port_(port),
      sda_gpio_(sda_gpio),
      scl_gpio_(scl_gpio),
      address_(address),
      speed_hz_(speed_hz) {}

Mpu6050::~Mpu6050() {
    if (device_ != nullptr) {
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }
    if (bus_ != nullptr) {
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
}

bool Mpu6050::WriteRegister(uint8_t reg, uint8_t value) {
    uint8_t buffer[2] = {reg, value};
    esp_err_t err = i2c_master_transmit(device_, buffer, sizeof(buffer), kI2cTimeoutMs);
    if (err != ESP_OK) {
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_error_log_us_ > 1000000) {
            last_error_log_us_ = now_us;
            ESP_LOGW(TAG, "write reg 0x%02X failed: %s", reg, esp_err_to_name(err));
        }
        return false;
    }
    return true;
}

bool Mpu6050::ReadRegisters(uint8_t reg, uint8_t* data, size_t len) {
    esp_err_t err =
        i2c_master_transmit_receive(device_, &reg, 1, data, len, kI2cTimeoutMs);
    if (err != ESP_OK) {
        // I2C 接触不良时避免 20Hz 刷屏，每秒最多打印一次
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_error_log_us_ > 1000000) {
            last_error_log_us_ = now_us;
            ESP_LOGW(TAG, "read reg 0x%02X failed: %s", reg, esp_err_to_name(err));
        }
        return false;
    }
    return true;
}

bool Mpu6050::CalibrateGyro() {
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    float sum_z = 0.0f;
    float sum2_x = 0.0f;
    float sum2_y = 0.0f;
    float sum2_z = 0.0f;

    for (int i = 0; i < kGyroCalibrationSamples; ++i) {
        uint8_t data[14];
        if (!ReadRegisters(kRegAccelXoutH, data, sizeof(data))) {
            return false;
        }
        float raw_x = static_cast<int16_t>((data[8] << 8) | data[9]);
        float raw_y = static_cast<int16_t>((data[10] << 8) | data[11]);
        float raw_z = static_cast<int16_t>((data[12] << 8) | data[13]);
        sum_x += raw_x;
        sum_y += raw_y;
        sum_z += raw_z;
        sum2_x += raw_x * raw_x;
        sum2_y += raw_y * raw_y;
        sum2_z += raw_z * raw_z;
        vTaskDelay(pdMS_TO_TICKS(2));
    }

    gyro_bias_x_ = sum_x / kGyroCalibrationSamples;
    gyro_bias_y_ = sum_y / kGyroCalibrationSamples;
    gyro_bias_z_ = sum_z / kGyroCalibrationSamples;

    float var_x = std::max(0.0f, sum2_x / kGyroCalibrationSamples - gyro_bias_x_ * gyro_bias_x_);
    float var_y = std::max(0.0f, sum2_y / kGyroCalibrationSamples - gyro_bias_y_ * gyro_bias_y_);
    float var_z = std::max(0.0f, sum2_z / kGyroCalibrationSamples - gyro_bias_z_ * gyro_bias_z_);
    float std_x = std::sqrt(var_x) / kGyroScale;
    float std_y = std::sqrt(var_y) / kGyroScale;
    float std_z = std::sqrt(var_z) / kGyroScale;

    // 用 WARN 输出，避免遥测把全局日志级别降到 WARN 后看不到校准结果
    ESP_LOGW(TAG, "gyro bias (raw): x=%.1f y=%.1f z=%.1f", gyro_bias_x_, gyro_bias_y_,
             gyro_bias_z_);
    if (std_x > 2.0f || std_y > 2.0f || std_z > 2.0f) {
        ESP_LOGW(TAG, "MPU6050 moved during calibration (std=%.1f/%.1f/%.1f deg/s), reboot while still",
                 std_x, std_y, std_z);
    }
    return true;
}

bool Mpu6050::Init() {
    i2c_master_bus_config_t bus_config = {
        .i2c_port = port_,
        .sda_io_num = sda_gpio_,
        .scl_io_num = scl_gpio_,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .intr_priority = 0,
        .trans_queue_depth = 0,
        .flags =
            {
                .enable_internal_pullup = 1,
                .allow_pd = 0,
            },
    };
    if (i2c_new_master_bus(&bus_config, &bus_) != ESP_OK) {
        ESP_LOGE(TAG, "i2c bus init failed");
        return false;
    }

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address_,
        .scl_speed_hz = speed_hz_,
        .scl_wait_us = 0,
        .flags =
            {
                .disable_ack_check = 0,
            },
    };
    if (i2c_master_bus_add_device(bus_, &device_config, &device_) != ESP_OK) {
        ESP_LOGE(TAG, "add i2c device failed");
        return false;
    }

    uint8_t who_am_i = 0;
    if (!ReadRegisters(kRegWhoAmI, &who_am_i, 1)) {
        ESP_LOGE(TAG, "read WHO_AM_I failed");
        return false;
    }
    if (who_am_i == 0x00 || who_am_i == 0xFF) {
        ESP_LOGE(TAG, "MPU6050 not found (WHO_AM_I=0x%02X)", who_am_i);
        return false;
    }
    // 0x70/0x71/0x73 多为 MPU6500/MPU9250 系列，温度换算公式不同
    is_mpu6500_ = (who_am_i == 0x70 || who_am_i == 0x71 || who_am_i == 0x73);
    ESP_LOGI(TAG, "WHO_AM_I=0x%02X", who_am_i);

    // 复位后唤醒，使用 X 轴陀螺 PLL 作为时钟源
    if (!WriteRegister(kRegPwrMgmt1, 0x80)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
    if (!WriteRegister(kRegPwrMgmt1, 0x01)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // 采样率 100Hz，DLPF 44Hz/42Hz，陀螺 ±500°/s，加速度 ±2g
    if (!WriteRegister(kRegSmplrtDiv, 0x09) || !WriteRegister(kRegConfig, 0x03) ||
        !WriteRegister(kRegGyroConfig, 0x08) || !WriteRegister(kRegAccelConfig, 0x00)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    if (!CalibrateGyro()) {
        ESP_LOGW(TAG, "gyro calibration failed, yaw may drift faster");
    }

    // 用第一帧加速度初始化姿态，避免从 0 开始收敛
    uint8_t data[6];
    if (ReadRegisters(kRegAccelXoutH, data, sizeof(data))) {
        float ax = static_cast<int16_t>((data[0] << 8) | data[1]) / kAccelScale;
        float ay = static_cast<int16_t>((data[2] << 8) | data[3]) / kAccelScale;
        float az = static_cast<int16_t>((data[4] << 8) | data[5]) / kAccelScale;
        ResetFromAccel(ax, ay, az);
    }
    last_update_us_ = esp_timer_get_time();
    ESP_LOGI(TAG, "MPU6050 initialized (Mahony filter)");
    return true;
}

void Mpu6050::ResetFromAccel(float ax, float ay, float az) {
    float roll = std::atan2(ay, az);
    float pitch = std::atan2(-ax, std::sqrt(ay * ay + az * az));

    float cr = std::cos(roll * 0.5f);
    float sr = std::sin(roll * 0.5f);
    float cp = std::cos(pitch * 0.5f);
    float sp = std::sin(pitch * 0.5f);

    // yaw 初始为 0
    q0_ = cr * cp;
    q1_ = sr * cp;
    q2_ = cr * sp;
    q3_ = -sr * sp;

    // 内部四元数按 nose-down 为正；对外的 pitch 按上位机规范（机头抬起为正）取反
    pitch_ = -pitch * kRadToDeg;
    roll_ = roll * kRadToDeg;
    yaw_ = 0.0f;
    integral_fb_x_ = 0.0f;
    integral_fb_y_ = 0.0f;
}

bool Mpu6050::Update() {
    uint8_t data[14];
    if (!ReadRegisters(kRegAccelXoutH, data, sizeof(data))) {
        return false;
    }

    float ax = static_cast<int16_t>((data[0] << 8) | data[1]) / kAccelScale;
    float ay = static_cast<int16_t>((data[2] << 8) | data[3]) / kAccelScale;
    float az = static_cast<int16_t>((data[4] << 8) | data[5]) / kAccelScale;

    float gx = (static_cast<int16_t>((data[8] << 8) | data[9]) - gyro_bias_x_) / kGyroScale;
    float gy = (static_cast<int16_t>((data[10] << 8) | data[11]) - gyro_bias_y_) / kGyroScale;
    float gz = (static_cast<int16_t>((data[12] << 8) | data[13]) - gyro_bias_z_) / kGyroScale;

    if (!std::isfinite(ax) || !std::isfinite(ay) || !std::isfinite(az) || !std::isfinite(gx) ||
        !std::isfinite(gy) || !std::isfinite(gz)) {
        return false;
    }

    // 保存给上位机遥测用的原始物理量（在 Mahony 修正之前）
    ax_ = ax;
    ay_ = ay;
    az_ = az;
    gx_ = gx;
    gy_ = gy;
    gz_ = gz;

    int16_t raw_temperature = static_cast<int16_t>((data[6] << 8) | data[7]);
    temperature_ = is_mpu6500_ ? raw_temperature / 333.87f + 21.0f
                               : raw_temperature / 340.0f + 36.53f;

    int64_t now_us = esp_timer_get_time();
    float dt = (now_us - last_update_us_) / 1000000.0f;
    last_update_us_ = now_us;
    // 任务被高优先级任务延迟时也要保留足够的积分时间，否则转动会被“吃掉”
    if (dt < 0.001f) {
        dt = 0.001f;
    } else if (dt > 0.5f) {
        dt = 0.5f;
    }

    // Mahony 滤波：用加速度计估计的重力方向修正陀螺零偏。
    // 这里不做 |a|≈1g 的门控：即使加速度计比例略有偏差，归一化后的方向仍然可用；
    // 门控会导致静止时完全失去修正，pitch/roll 被陀螺零偏带着一直漂。
    float accel_norm = std::sqrt(ax * ax + ay * ay + az * az);
    if (accel_norm > 0.01f) {
        float inv_norm = 1.0f / accel_norm;
        float axn = ax * inv_norm;
        float ayn = ay * inv_norm;
        float azn = az * inv_norm;

        float vx = 2.0f * (q1_ * q3_ - q0_ * q2_);
        float vy = 2.0f * (q0_ * q1_ + q2_ * q3_);
        float vz = q0_ * q0_ - q1_ * q1_ - q2_ * q2_ + q3_ * q3_;

        float ex = ayn * vz - azn * vy;
        float ey = azn * vx - axn * vz;

        // Mahony 的误差修正量单位是 rad/s，而 gx/gy 是 °/s，
        // 必须乘 RAD_TO_DEG；否则修正强度会小 57 倍，pitch/roll 会被零偏带着一直漂。
        float kp_x_dps = kMahonyKp * ex * kRadToDeg;
        float kp_y_dps = kMahonyKp * ey * kRadToDeg;

        // 积分反馈补偿残余零偏，只作用于 pitch/roll，单位同为 °/s
        integral_fb_x_ += kMahonyKi * ex * kRadToDeg * dt;
        integral_fb_y_ += kMahonyKi * ey * kRadToDeg * dt;
        integral_fb_x_ = std::clamp(integral_fb_x_, -kMaxIntegralFb, kMaxIntegralFb);
        integral_fb_y_ = std::clamp(integral_fb_y_, -kMaxIntegralFb, kMaxIntegralFb);

        // 6 轴没有磁力计，不用加速度计修正 yaw，避免把 yaw 拉偏
        gx += integral_fb_x_ + kp_x_dps;
        gy += integral_fb_y_ + kp_y_dps;
    }

    float gx_rad = gx * kDegToRad;
    float gy_rad = gy * kDegToRad;
    float gz_rad = gz * kDegToRad;
    float half_dt = 0.5f * dt;

    float q0 = q0_;
    float q1 = q1_;
    float q2 = q2_;
    float q3 = q3_;
    q0_ += (-q1 * gx_rad - q2 * gy_rad - q3 * gz_rad) * half_dt;
    q1_ += (q0 * gx_rad + q2 * gz_rad - q3 * gy_rad) * half_dt;
    q2_ += (q0 * gy_rad - q1 * gz_rad + q3 * gx_rad) * half_dt;
    q3_ += (q0 * gz_rad + q1 * gy_rad - q2 * gx_rad) * half_dt;

    float q_norm = std::sqrt(q0_ * q0_ + q1_ * q1_ + q2_ * q2_ + q3_ * q3_);
    if (q_norm > 0.0f) {
        q0_ /= q_norm;
        q1_ /= q_norm;
        q2_ /= q_norm;
        q3_ /= q_norm;
    }

    // 四元数 -> 欧拉角（ZYX），yaw 仍然没有绝对参考
    // 内部 pitch 为 nose-down 为正，按上位机规范取反为“机头抬起为正”
    pitch_ = -std::asin(std::clamp(2.0f * (q0_ * q2_ - q3_ * q1_), -1.0f, 1.0f)) * kRadToDeg;
    roll_ = std::atan2(2.0f * (q0_ * q1_ + q2_ * q3_),
                       1.0f - 2.0f * (q1_ * q1_ + q2_ * q2_)) *
            kRadToDeg;
    yaw_ = std::atan2(2.0f * (q0_ * q3_ + q1_ * q2_),
                      1.0f - 2.0f * (q2_ * q2_ + q3_ * q3_)) *
           kRadToDeg;
    return true;
}

#endif  // AI_SYSU_ENABLE_IMU
