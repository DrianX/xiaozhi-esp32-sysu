#include "VoltageMonitor.h"
#include "RGBController.h"

// 初始化电压检测相关全局变量（与头文件声明对应）
uint16_t bat_check_num = 0;
esp_adc_cal_characteristics_t adc_chars;

/**
 * @brief 电压检测初始化（原代码逻辑完全保留）
 */
void adc_calibration_init()
{
  if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_TP) == ESP_OK)  {
    printf("eFuse Two Point: Supported\n");
  }  else  {
    printf("eFuse Two Point: NOT supported\n");
  }
  // Check Vref is burned into eFuse
  if (esp_adc_cal_check_efuse(ESP_ADC_CAL_VAL_EFUSE_VREF) == ESP_OK)  {
    printf("eFuse Vref: Supported\n");
  }  else  {
    printf("eFuse Vref: NOT supported\n");
  }
}

/**
 * @brief 电压检测逻辑（原代码逻辑完全保留）
 */
void bat_check()
{
  if (bat_check_num > 1000)
  {
    // 电压读取
    uint32_t sum = 0;
    sum = analogRead(BAT_PIN);
    uint32_t voltage = esp_adc_cal_raw_to_voltage(sum, &adc_chars);
    double battery = (voltage * 3.97) / 1000.0;

    // 电量显示（LED_BAT引脚控制）
    if (battery >= 7.8){
      digitalWrite(LED_BAT, HIGH);
    }else{
      digitalWrite(LED_BAT, LOW);

      startLEDBlink(CRGB::Red, 200, 5); // rgb 红灯闪烁
    }
    bat_check_num = 0;
  }
  else
    bat_check_num++;
}