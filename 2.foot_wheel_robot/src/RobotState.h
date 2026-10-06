/* ---------------------------------
 * 机器人共享状态：舵机参数 + Wrobot 运动状态结构体
 * main.cpp 与 SerialCommand.cpp 共用
------------------------------------- */
#ifndef ROBOT_STATE_H
#define ROBOT_STATE_H

#include <Arduino.h>

/************ 舵机位置/速度/加速度参数 *************/
#define SERVO0_MIN  2047 // 舵机1最低位置，大腿与小腿碰到一起， 从小车正面看右手边舵机
#define SERVO1_MIN  2047 // 舵机2最低位置，大腿与小腿碰到一起， 从小车正面看左手边舵机
#define SERVO0_MAX (2047 + 12 + 8.4 * (35 + 10)) // 2438 舵机1最高
#define SERVO1_MAX (2047 - 12 - 8.4 * (35 + 10)) // 1658 舵机2最高
#define SERVO0_ACC 100 // 舵机1加速度，不能太快，否则影响其他动作平衡
#define SERVO1_ACC 100 // 舵机2加速度
#define SERVO0_SPEED 400 // 舵机1速度，不能太快，否则影响其他动作平衡
#define SERVO1_SPEED 400 // 舵机2速度

/************ 小车运动状态结构体 *************/
typedef struct
{
  int height = 30;
  int roll;
  int jump = 0; // 0=不跳跃 1=跳跃
  int joyy;
  int joyy_last;
  int joyx;
  int joyx_last;
  int acc0 =  SERVO0_ACC;    // 舵机1加速度
  int acc1 =  SERVO1_ACC;    // 舵机2加速度
  int speed0 = SERVO0_SPEED;  // 舵机1速度
  int speed1 = SERVO1_SPEED;  // 舵机2速度
} Wrobot;

extern Wrobot wrobot;  // 实例在 main.cpp 中定义

#endif
