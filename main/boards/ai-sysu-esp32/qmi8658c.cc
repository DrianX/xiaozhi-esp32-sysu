#include "config.h"

#if AI_SYSU_ENABLE_IMU

#include "qmi8658c.h"

#include <algorithm>
#include <cmath>

#include <esp_log.h>
#include <esp_timer.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

static const char* TAG = "QMI8658C";

namespace {
constexpr uint8_t kRegWhoAmI = 0x00;   // 固定为 0x05
constexpr uint8_t kRegCtrl1 = 0x02;    // 接口 / 地址自增
constexpr uint8_t kRegCtrl2 = 0x03;    // 加速度计配置
constexpr uint8_t kRegCtrl3 = 0x04;    // 陀螺仪配置
constexpr uint8_t kRegCtrl7 = 0x08;    // 传感器使能
constexpr uint8_t kRegReset = 0x60;
constexpr uint8_t kRegDataStart = 0x33;  // TEMP + ACC + GYRO，共 14 字节
constexpr uint8_t kWhoAmIValue = 0x05;
constexpr uint8_t kResetValue = 0xB0;

// CTRL1: 地址自增，小端
constexpr uint8_t kCtrl1Config = 0x40;
// CTRL2: aFS=±4g (bits6:4=001)，aODR=125Hz (0x06)
constexpr uint8_t kCtrl2Config = 0x16;
// CTRL3: gFS=±512dps (bits6:4=101)，gODR=125Hz (0x06)
constexpr uint8_t kCtrl3Config = 0x56;
// CTRL7: aEN | gEN
constexpr uint8_t kCtrl7Config = 0x03;

// ±4g -> 4096 LSB/g；±512dps -> 64 LSB/(°/s)
constexpr float kAccelScale = 4096.0f;
constexpr float kGyroScale = 64.0f;
constexpr float kRadToDeg = 57.2957795f;
constexpr float kDegToRad = 0.0174532925f;
constexpr float kMahonyKp = 1.0f;
constexpr float kMahonyKi = 0.02f;
constexpr float kMaxIntegralFb = 10.0f;  // °/s
constexpr int kI2cTimeoutMs = 100;
constexpr int kGyroCalibrationSamples = 500;
constexpr size_t kDataLength = 14;  // temp(2) + acc(6) + gyro(6)
}  // namespace

Qmi8658c::Qmi8658c(i2c_port_num_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio,
                   uint8_t address, uint32_t speed_hz)
    : port_(port),
      sda_gpio_(sda_gpio),
      scl_gpio_(scl_gpio),
      address_(address),
      speed_hz_(speed_hz) {}

Qmi8658c::~Qmi8658c() {
    if (device_ != nullptr) {
        i2c_master_bus_rm_device(device_);
        device_ = nullptr;
    }
    if (bus_ != nullptr) {
        i2c_del_master_bus(bus_);
        bus_ = nullptr;
    }
}

bool Qmi8658c::WriteRegister(uint8_t reg, uint8_t value) {
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

bool Qmi8658c::ReadRegisters(uint8_t reg, uint8_t* data, size_t len) {
    esp_err_t err = i2c_master_transmit_receive(device_, &reg, 1, data, len, kI2cTimeoutMs);
    if (err != ESP_OK) {
        // I2C 接触不良时避免 50Hz 刷屏，每秒最多打印一次
        int64_t now_us = esp_timer_get_time();
        if (now_us - last_error_log_us_ > 1000000) {
            last_error_log_us_ = now_us;
            ESP_LOGW(TAG, "read reg 0x%02X failed: %s", reg, esp_err_to_name(err));
        }
        return false;
    }
    return true;
}

bool Qmi8658c::CalibrateGyro() {
    float sum_x = 0.0f;
    float sum_y = 0.0f;
    float sum_z = 0.0f;
    float sum2_x = 0.0f;
    float sum2_y = 0.0f;
    float sum2_z = 0.0f;

    for (int i = 0; i < kGyroCalibrationSamples; ++i) {
        uint8_t data[kDataLength];
        if (!ReadRegisters(kRegDataStart, data, sizeof(data))) {
            return false;
        }
        float raw_x = static_cast<int16_t>((data[9] << 8) | data[8]);
        float raw_y = static_cast<int16_t>((data[11] << 8) | data[10]);
        float raw_z = static_cast<int16_t>((data[13] << 8) | data[12]);
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
        ESP_LOGW(TAG,
                 "QMI8658C moved during calibration (std=%.1f/%.1f/%.1f deg/s), reboot while still",
                 std_x, std_y, std_z);
    }
    return true;
}

void Qmi8658c::ResetFromAccel(float ax, float ay, float az) {
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

bool Qmi8658c::Init() {
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

    // QMI8658C 的 I2C 地址由 SA0 决定：0x6A 或 0x6B。两个都试一下。
    bool found = false;
    for (int attempt = 0; attempt < 2 && !found; ++attempt) {
        uint8_t candidate = address_ ^ static_cast<uint8_t>(attempt);
        i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = candidate,
            .scl_speed_hz = speed_hz_,
            .scl_wait_us = 0,
            .flags =
                {
                    .disable_ack_check = 0,
                },
        };
        if (i2c_master_bus_add_device(bus_, &device_config, &device_) != ESP_OK) {
            device_ = nullptr;
            continue;
        }

        uint8_t who_am_i = 0;
        if (ReadRegisters(kRegWhoAmI, &who_am_i, 1) && who_am_i == kWhoAmIValue) {
            address_ = candidate;
            found = true;
            ESP_LOGI(TAG, "WHO_AM_I=0x%02X at 0x%02X", who_am_i, candidate);
        } else {
            i2c_master_bus_rm_device(device_);
            device_ = nullptr;
        }
    }
    if (!found) {
        ESP_LOGE(TAG, "QMI8658C not found (tried 0x%02X/0x%02X)", address_,
                 address_ ^ 0x01);
        return false;
    }

    // 复位
    if (!WriteRegister(kRegReset, kResetValue)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(50));

    // 地址自增、小端；加速度 ±4g / 125Hz；陀螺 ±512dps / 125Hz；使能两轴
    if (!WriteRegister(kRegCtrl1, kCtrl1Config) || !WriteRegister(kRegCtrl2, kCtrl2Config) ||
        !WriteRegister(kRegCtrl3, kCtrl3Config) || !WriteRegister(kRegCtrl7, kCtrl7Config)) {
        return false;
    }
    vTaskDelay(pdMS_TO_TICKS(100));

    if (!CalibrateGyro()) {
        ESP_LOGW(TAG, "gyro calibration failed, yaw may drift faster");
    }

    // 用第一帧加速度初始化姿态，避免从 0 开始收敛
    uint8_t data[kDataLength];
    if (ReadRegisters(kRegDataStart, data, sizeof(data))) {
        float ax = static_cast<int16_t>((data[3] << 8) | data[2]) / kAccelScale;
        float ay = static_cast<int16_t>((data[5] << 8) | data[4]) / kAccelScale;
        float az = static_cast<int16_t>((data[7] << 8) | data[6]) / kAccelScale;
        ResetFromAccel(ax, ay, az);
    }
    last_update_us_ = esp_timer_get_time();
    ESP_LOGI(TAG, "QMI8658C initialized (Mahony filter)");
    return true;
}

bool Qmi8658c::Update() {
    uint8_t data[kDataLength];
    if (!ReadRegisters(kRegDataStart, data, sizeof(data))) {
        return false;
    }

    int16_t raw_temp = static_cast<int16_t>((data[1] << 8) | data[0]);
    float ax = static_cast<int16_t>((data[3] << 8) | data[2]) / kAccelScale;
    float ay = static_cast<int16_t>((data[5] << 8) | data[4]) / kAccelScale;
    float az = static_cast<int16_t>((data[7] << 8) | data[6]) / kAccelScale;

    float gx = (static_cast<int16_t>((data[9] << 8) | data[8]) - gyro_bias_x_) / kGyroScale;
    float gy = (static_cast<int16_t>((data[11] << 8) | data[10]) - gyro_bias_y_) / kGyroScale;
    float gz = (static_cast<int16_t>((data[13] << 8) | data[12]) - gyro_bias_z_) / kGyroScale;

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
    temperature_ = raw_temp / 256.0f;

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
    // 不做 |a|≈1g 门控：即使加速度计比例略有偏差，归一化后的方向仍然可用。
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
