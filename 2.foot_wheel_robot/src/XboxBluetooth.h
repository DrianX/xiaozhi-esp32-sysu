/* ---------------------------------
 * 蓝牙手柄（Xbox）模块
 * 包含：蓝牙实体、状态变量、连接处理函数
------------------------------------- */
#ifndef XBOX_BLUETOOTH_H
#define XBOX_BLUETOOTH_H

#include <Arduino.h>
#include <XboxSeriesXControllerESP32_asukiaaa.hpp>

/************ 全局实体与状态变量 *************/
XboxSeriesXControllerESP32_asukiaaa::Core xboxController("3c:fa:06:4f:33:55");
XboxSeriesXHIDReportBuilder_asukiaaa::ReportBase repo;

// 跟踪手柄连接状态（用于检测首次连接和断开）
bool wasConnected = 0;  // 是否链接

// 震动状态机（关键：记录当前处于震动的哪个阶段）
enum VibrateState {
  VIBRATE_OFF,       // 未震动
  VIBRATE_STARTED,   // 已发送开启指令，等待关闭
  VIBRATE_TRIGGER    // 扳机键震动状态（单独管理）
};
VibrateState vibrateState = VIBRATE_OFF;
unsigned long vibrateStartMs = 0;  // 震动开始的时间戳
unsigned long long xbox_vibrate_duration = 200;  // 按键震动持续时间 （可被函数动态更新）

/************ 外部函数声明 *************/
extern void processControllerData(const XboxControllerNotificationParser& data);

/************ 核心函数 *************/

void xboxControllerInit() {
  xboxController.begin();
  vibrateState = VIBRATE_OFF;  // 初始化状态
}

// 非阻塞震动状态更新（必须在主循环中调用）
void updateXboxVibration() {
  unsigned long currentMs = millis();
  
  // 处理按键震动的"开启→等待→关闭"流程
  if (vibrateState == VIBRATE_STARTED || vibrateState == VIBRATE_TRIGGER) {
    // 检查是否达到震动持续时间
    if (currentMs - vibrateStartMs >= xbox_vibrate_duration) {
      // 执行原函数中delay后的关闭逻辑
      repo.v.power.center = 0;
      repo.v.power.shake = 0;
      xboxController.writeHIDReport(repo);
      vibrateState = VIBRATE_OFF;  // 回到未震动状态
    }
  }
}

// 按键震动（完整复现原逻辑的非阻塞版本）
void XboxKeyVibration(uint8_t power = 20, unsigned long duration = 200) {

  power = constrain(power, 0, 100); // 约束功率范围（防止无效值，确保在0-100之间）
  duration = constrain(duration, 50, 2000);  // 时长限制50-2000ms（合理范围）

  // 核心：将传入的时长（默认200或自定义）赋值给全局变量
  xbox_vibrate_duration = duration;

  // 若正在震动，先重置状态（避免重复触发导致混乱）
  if (vibrateState != VIBRATE_OFF) {
    repo.v.power.center = 0;
    repo.v.power.shake = 0;
    xboxController.writeHIDReport(repo);
  }
  
  // 执行原函数中delay前的开启逻辑
  repo.setAllOff();
  repo.v.select.center = true;
  repo.v.select.left = true;
  repo.v.select.right = true;
  repo.v.select.shake = true;
  repo.v.power.center = power; // 20% power
  repo.v.power.shake = power;
  repo.v.timeActive = 10;  // x/100 second
  xboxController.writeHIDReport(repo);
  
  // 记录开始时间，进入"等待关闭"状态（替代delay(250)）
  vibrateState = VIBRATE_STARTED;
  vibrateStartMs = millis();
}

// 扳机键震动（同样保留完整时序）
void XboxtrigRTVibration( unsigned long duration = 300) {
  static uint16_t TrigMax = XboxControllerNotificationParser::maxTrig;
  uint8_t power = (uint8_t)((float)xboxController.xboxNotif.trigRT / TrigMax * 100) * 0.5;
  
  // 核心：将传入的时长（默认300或自定义）赋值给全局变量
  xbox_vibrate_duration = duration;

  // 若扳机键松开，直接关闭震动
  if (power == 0) {
    repo.v.power.center = 0;
    repo.v.power.shake = 0;
    xboxController.writeHIDReport(repo);
    vibrateState = VIBRATE_OFF;
    return;
  }
  
  // 执行开启震动逻辑
  repo.setAllOff();
  repo.v.select.center = true;
  repo.v.select.left = true;
  repo.v.select.right = true;
  repo.v.select.shake = true;
  repo.v.power.center = power;
  repo.v.power.shake = power;
  repo.v.timeActive = 10;
  xboxController.writeHIDReport(repo);
  
  // 进入扳机震动状态，等待关闭
  vibrateState = VIBRATE_TRIGGER;
  vibrateStartMs = millis();
}

void process_xbox_notif() {
  xboxController.onLoop();
  if (xboxController.isConnected() && !xboxController.isWaitingForFirstNotification()) {
    if(!wasConnected){ // 原来未链接，现在链接成功
        XboxKeyVibration(100,1000);
        startLEDBlink(CRGB::Green, 50, 3);
        wasConnected = 1;
    }
    processControllerData(xboxController.xboxNotif);
  }else{
     if(wasConnected){ // 原来是链接成功的，说明断了
        XboxKeyVibration(80);
        wasConnected = 0;
    }
  }
  
}

#endif  // XBOX_BLUETOOTH_H