/* ---------------------------------
 * 串口控制模块（外接控制板，如小智 AI）
 * 硬件：RX=IO4（接控制板 TX），TX=IO15（接控制板 RX）
 * 协议：英文关键词指令，换行结尾，详见 README-串口控制.md
------------------------------------- */
#ifndef SERIAL_COMMAND_H
#define SERIAL_COMMAND_H

#include <Arduino.h>

/************ 硬件配置 *************/
#define CTRL_RX_PIN 4     // 小车 RX，接控制板 TX
#define CTRL_TX_PIN 15    // 小车 TX，接控制板 RX
#define CTRL_BAUD 115200

/************ 函数声明 *************/
void serialCommandInit();       // setup 中调用：初始化 Serial1
void serialCommandSendReady();  // 初始化完成后调用：向控制板广播 READY
void serialCommandProcess();    // loop 中调用：解析指令 + 定时自动停止

#endif
