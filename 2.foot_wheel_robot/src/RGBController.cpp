/* ---------------------------------
 * LED 控制模块实现

// 开始红色闪烁，每次持续200毫秒，闪烁3次
startLEDBlink(CRGB::Red, 200, 3);

// 开始蓝色闪烁，每次持续500毫秒，闪烁5次
startLEDBlink(CRGB::Blue, 500, 5);

// 停止闪烁
stopLEDBlink();

// 设置LED为绿色常亮
setLEDColor(CRGB::Green);
------------------------------------- */
#include "RGBController.h"

// LED 全局变量定义（与主文件解耦）
CRGB leds[NUM_LEDS];
unsigned long previousBlinkMillis = 0;
int blinkState = 0;
int blinkCount = 0;
bool isBlinking = false;
CRGB blinkColors[6] = {CRGB::Red, CRGB::Red, CRGB::Yellow, CRGB::Yellow, CRGB::Green, CRGB::Green};

// 新增全局变量定义
unsigned long blinkDuration = 200;       // 默认闪烁持续时间200毫秒
unsigned long blinkInterval = 200;       // 默认闪烁间隔时间200毫秒
CRGB currentBlinkColor = CRGB::Red;      // 默认红色
int totalBlinkCount = 3;                 // 默认闪烁3次
bool useColorSequence = false;           // 默认不使用颜色序列模式

// 初始化LED（替代原setup中的FastLED初始化代码）
void initLEDs() {
  FastLED.addLeds<WS2812, RGB_PIN, GRB>(leds, NUM_LEDS);
  FastLED.setBrightness(50);
  fill_solid(leds, NUM_LEDS, CRGB::Black);  // 初始熄灭
  FastLED.show();
}

// 开始LED闪烁
void startLEDBlink(CRGB color, unsigned long duration, int count) {
  currentBlinkColor = color;      // 设置闪烁颜色
  blinkDuration = duration;       // 设置闪烁持续时间
  blinkInterval = duration;       // 设置闪烁间隔时间（与持续时间相同）
  totalBlinkCount = count * 2;   // 设置总闪烁次数（亮灭算一次）
  useColorSequence = false;       // 使用单色模式
  
  blinkCount = 0;                 // 重置当前闪烁计数
  blinkState = 0;                 // 重置闪烁状态
  isBlinking = true;              // 开始闪烁
  previousBlinkMillis = millis(); // 重置时间戳
}

// 开始颜色序列闪烁
void startColorSequenceBlink() {
  blinkDuration = 50;            // 设置闪烁持续时间
  blinkInterval = 50;            // 设置闪烁间隔时间
  totalBlinkCount = 6;            // 设置总闪烁次数（6种颜色）
  useColorSequence = true;         // 使用颜色序列模式
  
  blinkCount = 0;                 // 重置当前闪烁计数
  blinkState = 0;                 // 重置闪烁状态
  isBlinking = true;              // 开始闪烁
  previousBlinkMillis = millis(); // 重置时间戳
}

// 停止LED闪烁
void stopLEDBlink() {
  isBlinking = false;
  fill_solid(leds, NUM_LEDS, CRGB::Black);
  FastLED.show();
}

// 设置LED颜色
void setLEDColor(CRGB color) {
  fill_solid(leds, NUM_LEDS, color);
  FastLED.show();
}

// 处理LED闪烁逻辑（非阻塞实现）
void handleLEDBlink() {
  if (!isBlinking) return;
  
  unsigned long currentMillis = millis();
  
  // 检查是否达到切换时间
  if (currentMillis - previousBlinkMillis >= (blinkState == 0 ? blinkDuration : blinkInterval)) {
    previousBlinkMillis = currentMillis;
    
    if (blinkState == 0) {
      // LED亮起
      if (useColorSequence) {
        // 使用颜色序列模式
        fill_solid(leds, NUM_LEDS, blinkColors[blinkCount % 6]);
      } else {
        // 使用单色模式
        fill_solid(leds, NUM_LEDS, currentBlinkColor);
      }
      FastLED.show();
      blinkState = 1;
    } else {
      // LED熄灭
      fill_solid(leds, NUM_LEDS, CRGB::Black);
      FastLED.show();
      blinkState = 0;
      blinkCount++;
      
      // 检查是否完成所有闪烁
      if (blinkCount >= totalBlinkCount) {
        isBlinking = false;
        blinkCount = 0;
        fill_solid(leds, NUM_LEDS, CRGB::Black);
        FastLED.show();
      }
    }
  }
}