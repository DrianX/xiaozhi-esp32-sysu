#include "adc_battery_monitor.h"

#include <esp_log.h>

static const char* TAG = "AdcBatteryMonitor";

namespace {
constexpr adc_atten_t kAdcAtten = ADC_ATTEN_DB_12;
constexpr adc_bitwidth_t kAdcBitwidth = ADC_BITWIDTH_DEFAULT;
constexpr int kVoltageSampleCount = 16;
}  // namespace

AdcBatteryMonitor::AdcBatteryMonitor(adc_unit_t adc_unit, adc_channel_t adc_channel,
                                     float upper_resistor, float lower_resistor,
                                     gpio_num_t charging_pin, bool charging_active_low,
                                     float voltage_scale)
    : charging_pin_(charging_pin),
      charging_active_low_(charging_active_low),
      adc_channel_(adc_channel) {
    mutex_ = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(mutex_ != nullptr ? ESP_OK : ESP_ERR_NO_MEM);

    // Initialize charging pin (only if it's not NC)
    if (charging_pin_ != GPIO_NUM_NC) {
        gpio_config_t gpio_cfg = {
            .pin_bit_mask = 1ULL << charging_pin,
            .mode = GPIO_MODE_INPUT,
            // 开漏输出需要上拉才能读到高电平
            .pull_up_en = charging_active_low_ ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_ERROR_CHECK(gpio_config(&gpio_cfg));
    }

    // 自己创建 ADC，既传给电量估算组件，也用于读取原始电压
    adc_oneshot_unit_init_cfg_t init_cfg = {
        .unit_id = adc_unit,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_cfg, &adc_handle_));

    adc_oneshot_chan_cfg_t chan_cfg = {
        .atten = kAdcAtten,
        .bitwidth = kAdcBitwidth,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle_, adc_channel_, &chan_cfg));

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = adc_unit,
        .chan = adc_channel,
        .atten = kAdcAtten,
        .bitwidth = kAdcBitwidth,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle_));
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = adc_unit,
        .atten = kAdcAtten,
        .bitwidth = kAdcBitwidth,
    };
    ESP_ERROR_CHECK(adc_cali_create_scheme_line_fitting(&cali_config, &adc_cali_handle_));
#endif

    // 标定分压比：最终电压 = 计算值 * voltage_scale，
    // 即 lower'/(upper'+lower') = (lower/(upper+lower)) / scale。
    // 保持 lower 不变，取 upper' = scale * (upper + lower) - lower。
    float scale = (voltage_scale > 0.0f) ? voltage_scale : 1.0f;
    float calibrated_lower = lower_resistor;
    float calibrated_upper = scale * (upper_resistor + lower_resistor) - lower_resistor;
    if (calibrated_upper <= 0.0f) {
        calibrated_upper = 1.0f;
    }
    float total_resistance = calibrated_upper + calibrated_lower;
    if (total_resistance > 0.0f) {
        voltage_divider_ratio_ = calibrated_lower / total_resistance;
    }

    adc_battery_estimation_t adc_cfg = {
        .external =
            {
                .adc_handle = adc_handle_,
                .adc_cali_handle = adc_cali_handle_,
            },
        .adc_channel = adc_channel,
        .upper_resistor = calibrated_upper,
        .lower_resistor = calibrated_lower,
    };

    // ADC 由本类持有，组件只借用，不负责释放
    if (charging_pin_ != GPIO_NUM_NC) {
        adc_cfg.charging_detect_cb = [](void* user_data) -> bool {
            AdcBatteryMonitor* self = (AdcBatteryMonitor*)user_data;
            return self->ReadChargingPin();
        };
        adc_cfg.charging_detect_user_data = this;
    }
    adc_battery_estimation_handle_ = adc_battery_estimation_create(&adc_cfg);
    ESP_ERROR_CHECK(adc_battery_estimation_handle_ != nullptr ? ESP_OK : ESP_FAIL);

    // Initialize timer
    esp_timer_create_args_t timer_cfg = {
        .callback =
            [](void* arg) {
                AdcBatteryMonitor* self = (AdcBatteryMonitor*)arg;
                self->CheckBatteryStatus();
            },
        .arg = this,
        .name = "adc_battery_monitor",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_cfg, &timer_handle_));
    ESP_ERROR_CHECK(esp_timer_start_periodic(timer_handle_, 1000000));
}

AdcBatteryMonitor::~AdcBatteryMonitor() {
    if (timer_handle_) {
        esp_timer_stop(timer_handle_);
        esp_timer_delete(timer_handle_);
    }

    if (adc_battery_estimation_handle_) {
        ESP_ERROR_CHECK(adc_battery_estimation_destroy(adc_battery_estimation_handle_));
    }

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    if (adc_cali_handle_) {
        adc_cali_delete_scheme_curve_fitting(adc_cali_handle_);
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    if (adc_cali_handle_) {
        adc_cali_delete_scheme_line_fitting(adc_cali_handle_);
    }
#endif

    if (adc_handle_) {
        adc_oneshot_del_unit(adc_handle_);
    }

    if (mutex_ != nullptr) {
        vSemaphoreDelete(mutex_);
    }
}

bool AdcBatteryMonitor::IsCharging() {
    // 优先使用adc_battery_estimation库的功能
    if (adc_battery_estimation_handle_ != nullptr) {
        bool is_charging = false;
        esp_err_t err =
            adc_battery_estimation_get_charging_state(adc_battery_estimation_handle_, &is_charging);
        if (err == ESP_OK) {
            return is_charging;
        }
    }

    // 回退到GPIO读取或返回默认值
    if (charging_pin_ != GPIO_NUM_NC) {
        return ReadChargingPin();
    }

    return false;
}

bool AdcBatteryMonitor::ReadChargingPin() const {
    int level = gpio_get_level(charging_pin_);
    return charging_active_low_ ? (level == 0) : (level == 1);
}

bool AdcBatteryMonitor::IsDischarging() { return !IsCharging(); }

uint8_t AdcBatteryMonitor::GetBatteryLevel() {
    if (adc_battery_estimation_handle_ == nullptr) {
        return last_level_;
    }

    float capacity = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    esp_err_t err = adc_battery_estimation_get_capacity(adc_battery_estimation_handle_, &capacity);
    xSemaphoreGive(mutex_);

    if (err == ESP_OK) {
        last_level_ = (uint8_t)capacity;
    }
    return last_level_;
}

int AdcBatteryMonitor::GetBatteryVoltageMv() {
    if (adc_handle_ == nullptr || adc_cali_handle_ == nullptr || voltage_divider_ratio_ <= 0.0f) {
        return -1;
    }

    int sum_mv = 0;
    int valid_count = 0;
    xSemaphoreTake(mutex_, portMAX_DELAY);
    for (int i = 0; i < kVoltageSampleCount; ++i) {
        int raw = 0;
        int adc_mv = 0;
        if (adc_oneshot_read(adc_handle_, adc_channel_, &raw) == ESP_OK &&
            adc_cali_raw_to_voltage(adc_cali_handle_, raw, &adc_mv) == ESP_OK) {
            sum_mv += adc_mv;
            ++valid_count;
        }
    }
    xSemaphoreGive(mutex_);

    if (valid_count == 0) {
        return -1;
    }

    float average_mv = (float)sum_mv / valid_count;
    return (int)(average_mv / voltage_divider_ratio_ + 0.5f);
}

void AdcBatteryMonitor::OnChargingStatusChanged(std::function<void(bool)> callback) {
    on_charging_status_changed_ = callback;
}

void AdcBatteryMonitor::CheckBatteryStatus() {
    bool new_charging_status = IsCharging();
    if (new_charging_status != is_charging_) {
        is_charging_ = new_charging_status;
        if (on_charging_status_changed_) {
            on_charging_status_changed_(is_charging_);
        }
    }
}
