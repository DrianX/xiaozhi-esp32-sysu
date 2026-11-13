#ifndef _MPU6050_H_
#define _MPU6050_H_

#include <cstddef>
#include <cstdint>

#include <driver/gpio.h>
#include <driver/i2c_master.h>

// MPU6050 六轴姿态传感器驱动（I2C）。
// 上电时会做一次陀螺零偏校准，校准期间请保持模块静止。
// yaw 由陀螺 Z 轴积分得到，没有磁力计，长时间会缓慢漂移。
class Mpu6050 {
public:
    Mpu6050(i2c_port_num_t port, gpio_num_t sda_gpio, gpio_num_t scl_gpio,
            uint8_t address = 0x68, uint32_t speed_hz = 400000);
    ~Mpu6050();

    bool Init();
    bool Update();

    float pitch() const { return pitch_; }
    float roll() const { return roll_; }
    float yaw() const { return yaw_; }

    // 原始物理量（供上位机遥测使用）
    float ax() const { return ax_; }
    float ay() const { return ay_; }
    float az() const { return az_; }
    float gx() const { return gx_; }
    float gy() const { return gy_; }
    float gz() const { return gz_; }
    float temperature() const { return temperature_; }

private:
    bool WriteRegister(uint8_t reg, uint8_t value);
    bool ReadRegisters(uint8_t reg, uint8_t* data, size_t len);
    bool CalibrateGyro();
    void ResetFromAccel(float ax, float ay, float az);

    i2c_port_num_t port_;
    gpio_num_t sda_gpio_;
    gpio_num_t scl_gpio_;
    uint8_t address_;
    uint32_t speed_hz_;
    i2c_master_bus_handle_t bus_ = nullptr;
    i2c_master_dev_handle_t device_ = nullptr;

    int64_t last_update_us_ = 0;
    int64_t last_error_log_us_ = 0;
    float pitch_ = 0.0f;
    float roll_ = 0.0f;
    float yaw_ = 0.0f;
    float gyro_bias_x_ = 0.0f;
    float gyro_bias_y_ = 0.0f;
    float gyro_bias_z_ = 0.0f;
    // 最近一帧原始物理量
    float ax_ = 0.0f;
    float ay_ = 0.0f;
    float az_ = 0.0f;
    float gx_ = 0.0f;
    float gy_ = 0.0f;
    float gz_ = 0.0f;
    float temperature_ = 0.0f;
    bool is_mpu6500_ = false;
    // Mahony 四元数姿态
    float q0_ = 1.0f;
    float q1_ = 0.0f;
    float q2_ = 0.0f;
    float q3_ = 0.0f;
    // Mahony 积分反馈（只用于 pitch/roll，补偿残余陀螺零偏）
    float integral_fb_x_ = 0.0f;
    float integral_fb_y_ = 0.0f;
};

#endif  // _MPU6050_H_
