#ifndef ADC_BATTERY_MONITOR_H
#define ADC_BATTERY_MONITOR_H

#include <driver/gpio.h>
#include <esp_adc/adc_cali.h>
#include <esp_adc/adc_oneshot.h>
#include <esp_timer.h>
#include <adc_battery_estimation.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <functional>

class AdcBatteryMonitor {
public:
    // charging_active_low: 充电检测脚为开漏输出、充电时拉低时设为 true
    // voltage_scale: 电压标定系数，最终电压 = 计算值 * voltage_scale
    AdcBatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel, float upper_resistor,
                      float lower_resistor, gpio_num_t charging_pin = GPIO_NUM_NC,
                      bool charging_active_low = false, float voltage_scale = 1.0f);
    ~AdcBatteryMonitor();

    bool IsCharging();
    bool IsDischarging();
    uint8_t GetBatteryLevel();
    // 电池端电压（mV），读取失败返回 -1
    int GetBatteryVoltageMv();

    void OnChargingStatusChanged(std::function<void(bool)> callback);

private:
    gpio_num_t charging_pin_;
    bool charging_active_low_ = false;
    adc_battery_estimation_handle_t adc_battery_estimation_handle_ = nullptr;
    adc_oneshot_unit_handle_t adc_handle_ = nullptr;
    adc_cali_handle_t adc_cali_handle_ = nullptr;
    adc_channel_t adc_channel_;
    float voltage_divider_ratio_ = 1.0f;
    esp_timer_handle_t timer_handle_ = nullptr;
    SemaphoreHandle_t mutex_ = nullptr;
    bool is_charging_ = false;
    uint8_t last_level_ = 100;
    std::function<void(bool)> on_charging_status_changed_;

    bool ReadChargingPin() const;
    void CheckBatteryStatus();
};

#endif  // ADC_BATTERY_MONITOR_H
