/* ---------------------------------
 * RGB 控制模块
 * 包含：RGB初始化、闪烁逻辑、状态管理
------------------------------------- */
#ifndef RGB_CONTROLLER_H
#define RGB_CONTROLLER_H

#include <Arduino.h>
#include <FastLED.h>

// RGB 硬件配置（原代码中的宏定义）
#define RGB_PIN 21
#define NUM_LEDS 2

// LED 全局变量（原代码中的状态变量）
extern CRGB leds[NUM_LEDS];  // 外部声明，在主文件中定义
extern unsigned long previousBlinkMillis;
extern int blinkState;
extern int blinkCount;
extern bool isBlinking;
extern CRGB blinkColors[6];

// 新增全局变量
extern unsigned long blinkDuration;     // 闪烁持续时间（毫秒）
extern unsigned long blinkInterval;     // 闪烁间隔时间（毫秒）
extern CRGB currentBlinkColor;          // 当前闪烁颜色
extern int totalBlinkCount;             // 总闪烁次数
extern bool useColorSequence;            // 是否使用颜色序列闪烁模式

// LED 函数声明
void initLEDs();                        // 初始化LED
void handleLEDBlink();                   // 处理LED闪烁逻辑
void startLEDBlink(CRGB color, unsigned long duration, int count = 3); // 开始LED闪烁
void stopLEDBlink();                     // 停止LED闪烁
void setLEDColor(CRGB color);            // 设置LED颜色
void startColorSequenceBlink();          // 开始颜色序列闪烁

#endif  // RGB_CONTROLLER_H