#ifndef SERIAL_PARSER_H
#define SERIAL_PARSER_H

#include <Arduino.h>
#include <SimpleFOC.h>  // 包含 PIDController 类的定义
#include <FastLED.h>    // 包含 CRGB 类（如果用到 LED 相关）

// -------------------------- 全局对象声明（与 main.cpp 中定义一致）--------------------------
// 这些对象在 main.cpp 中定义，此处用 extern 声明供 SerialParser 访问
extern PIDController pid_super_balance;
extern PIDController pid_speed;
extern float zaccel_threshold;
extern float dragon_joyx_factor;


// extern float obstacle_pitch;
// extern int servo_up_height;
// extern int servo_down_height;
// extern float motor_target;  

extern float mid_obstacle_pitch ;
extern int mid_servo_up_height;
extern int mid_servo_down_height; 
extern float mid_motor_target;

extern float high_obstacle_pitch;
extern int high_servo_up_height;
extern int high_servo_down_height; 
extern float high_motor_target;

extern char serial_buf[32];
extern uint8_t buf_idx;

// -------------------------- 函数声明 --------------------------
void serialReceiveProcess();
void parseSingleParam(char* cmd);

#endif // SERIAL_PARSER_H