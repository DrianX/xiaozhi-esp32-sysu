/* ---------------------------------
 * 串口控制模块实现（外接控制板，如小智 AI）
 * 协议详见 README-串口控制.md
------------------------------------- */
#include "SerialCommand.h"
#include "RobotState.h"

#include <ctype.h>   // toupper
#include <stdlib.h>  // atoi, atol
#include <string.h>  // strcmp, strtok

// main.cpp 中定义的全局变量
extern bool robot_enabled;
extern int sitting_down;
extern int stand_up_count;
extern float leg_height_base;
extern int place_jump_flag;
extern int forward_jump_flag;
extern int back_jump_flag;
extern int left_jump_flag;
extern int right_jump_flag;
extern int jump_flag;
extern int jump_pre_flag;

/************ 默认参数 *************/
#define DEFAULT_SPEED 60           // 默认运动速度档位 1-100
#define SPIN_SPEED 100             // 转圈速度
#define SPIN_360_DURATION_MS 1000  // 转一圈的时长（毫秒），需实测校准
#define HEIGHT_STEP 10             // UP/DOWN 每档高度步长（leg_height_base 内部值）
#define READY_INTERVAL_MS 2000     // 未握手时 READY 广播间隔

/************ 内部状态 *************/
static char cmdBuf[32];
static uint8_t bufIdx = 0;
static int defaultSpeed = DEFAULT_SPEED;

static bool timedActive = false;   // 定时自动停止是否生效
static unsigned long timedStartMs = 0;
static unsigned long timedDurMs = 0;

static bool handshakeDone = false; // 是否已完成握手（起立）
static unsigned long lastReadyMs = 0;

static bool pendingJumpClear = false; // 跳跃触发只保留一个循环周期，之后清零

/************ 内部工具函数 *************/
static void replyOK() {
  Serial1.print("OK\r\n");
}

static void replyErr() {
  Serial1.print("ERR\r\n");
}

static void standUp() {
  robot_enabled = true;
  sitting_down = 0;
  stand_up_count = 1;
}

static void sitDown() {
  robot_enabled = false;
  sitting_down = 1;
}

static void stopMove() {
  wrobot.joyx = 0;
  wrobot.joyy = 0;
  wrobot.height = 50; // 停止时站到最高位
  timedActive = false;
}

// 设置运动量；速度越快车身越低保持稳定（与手柄逻辑一致），带时长则定时自动停止
static void setMove(int joyx, int joyy, long durMs) {
  wrobot.joyx = joyx;
  wrobot.joyy = joyy;
  int spd = max(abs(joyx), abs(joyy));
  wrobot.height = constrain(map(spd, 0, 100, 50, 30), 30, 50);
  if (durMs > 0) {
    timedActive = true;
    timedStartMs = millis();
    timedDurMs = (unsigned long)durMs;
  } else {
    timedActive = false;
  }
}

// 解析“速度 + 时长”两个可选参数：
//  - 两个参数：a1=速度(1-100)，a2=时长(ms)
//  - 单个参数：值 >100 视为时长(ms)，否则视为速度
static void parseSpeedDur(char* a1, char* a2, int& speed, long& dur) {
  speed = defaultSpeed;
  dur = 0;
  if (a1 && a2) {
    int s = atoi(a1);
    speed = constrain(s, 1, 100);
    dur = atol(a2);
  } else if (a1) {
    int v = atoi(a1);
    if (v > 100) dur = v;          // 单参数且 >100：视为时长
    else if (v > 0) speed = v;     // 否则视为速度
  }
}

/************ 命令处理 *************/
static void handleCommand(char* cmd) {
  // 按空格/逗号/制表符拆分：关键词 + 参数
  char* tok[4];
  int n = 0;
  char* p = strtok(cmd, " ,\t");
  while (p && n < 4) {
    tok[n++] = p;
    p = strtok(NULL, " ,\t");
  }
  if (n == 0) return;

  const char* kw = tok[0];

  // 握手：收到后自动起立
  if (!strcmp(kw, "HELLO") || !strcmp(kw, "HI") || !strcmp(kw, "START")) {
    handshakeDone = true;
    standUp();
    replyOK();
    return;
  }

  // 停止
  if (!strcmp(kw, "STOP")) {
    stopMove();
    replyOK();
    return;
  }

  // 前进
  if (!strcmp(kw, "FORWARD") || !strcmp(kw, "FWD") || !strcmp(kw, "GO")) {
    int speed;
    long dur;
    parseSpeedDur(n >= 2 ? tok[1] : NULL, n >= 3 ? tok[2] : NULL, speed, dur);
    setMove(0, speed, dur);
    replyOK();
    return;
  }

  // 后退
  if (!strcmp(kw, "BACK") || !strcmp(kw, "BACKWARD")) {
    int speed;
    long dur;
    parseSpeedDur(n >= 2 ? tok[1] : NULL, n >= 3 ? tok[2] : NULL, speed, dur);
    setMove(0, -speed, dur);
    replyOK();
    return;
  }

  // 左转（原地）
  if (!strcmp(kw, "LEFT")) {
    int speed;
    long dur;
    parseSpeedDur(n >= 2 ? tok[1] : NULL, n >= 3 ? tok[2] : NULL, speed, dur);
    setMove(speed, 0, dur);  // 正数 = 左转
    replyOK();
    return;
  }

  // 右转（原地）
  if (!strcmp(kw, "RIGHT")) {
    int speed;
    long dur;
    parseSpeedDur(n >= 2 ? tok[1] : NULL, n >= 3 ? tok[2] : NULL, speed, dur);
    setMove(-speed, 0, dur); // 负数 = 右转
    replyOK();
    return;
  }

  // 转圈（默认左转一圈，可指定方向/时长）
  if (!strcmp(kw, "SPIN")) {
    int dir = 1;               // 1=左 -1=右
    long dur = SPIN_360_DURATION_MS;
    if (n >= 2) {
      if (!strcmp(tok[1], "R") || !strcmp(tok[1], "RIGHT")) dir = -1;
      else if (!strcmp(tok[1], "L") || !strcmp(tok[1], "LEFT")) dir = 1;
      else dur = atol(tok[1]); // 第一个参数直接是时长
    }
    if (n >= 3) dur = atol(tok[2]);
    if (dur <= 0) dur = SPIN_360_DURATION_MS;
    setMove(SPIN_SPEED * dir, 0, dur);
    replyOK();
    return;
  }

  // 设置机身高度 0-100（0=最低 100=最高）
  if (!strcmp(kw, "HEIGHT")) {
    if (n >= 2) {
      int h = constrain(atoi(tok[1]), 0, 100);
      leg_height_base = map(h, 0, 100, 52, -10); // 内部：-10最高 52最低
      replyOK();
    } else {
      replyErr();
    }
    return;
  }

  // 升高 / 降低
  if (!strcmp(kw, "UP")) {
    leg_height_base = constrain(leg_height_base - HEIGHT_STEP, -10, 52);
    replyOK();
    return;
  }
  if (!strcmp(kw, "DOWN")) {
    leg_height_base = constrain(leg_height_base + HEIGHT_STEP, -10, 52);
    replyOK();
    return;
  }

  // 起立 / 坐下
  if (!strcmp(kw, "STAND") || !strcmp(kw, "STANDUP")) {
    standUp();
    replyOK();
    return;
  }
  if (!strcmp(kw, "SIT") || !strcmp(kw, "SITDOWN")) {
    sitDown();
    replyOK();
    return;
  }

  // 跳跃（默认原地跳，可指定方向 F/B/L/R）
  if (!strcmp(kw, "JUMP")) {
    if (!robot_enabled || sitting_down || jump_flag || jump_pre_flag) {
      replyErr();  // 坐下状态或已在跳跃中，不能跳
      return;
    }
    int dir = 0; // 0=原地 1=前 2=后 3=左 4=右
    if (n >= 2) {
      if (!strcmp(tok[1], "FORWARD") || !strcmp(tok[1], "F") || !strcmp(tok[1], "FRONT")) dir = 1;
      else if (!strcmp(tok[1], "BACK") || !strcmp(tok[1], "B") || !strcmp(tok[1], "BACKWARD")) dir = 2;
      else if (!strcmp(tok[1], "LEFT") || !strcmp(tok[1], "L")) dir = 3;
      else if (!strcmp(tok[1], "RIGHT") || !strcmp(tok[1], "R")) dir = 4;
      else { replyErr(); return; }
    }
    wrobot.jump = 1;
    if (dir == 1) forward_jump_flag = 1;
    else if (dir == 2) back_jump_flag = 1;
    else if (dir == 3) left_jump_flag = 1;
    else if (dir == 4) right_jump_flag = 1;
    else place_jump_flag = 1;
    pendingJumpClear = true;
    replyOK();
    return;
  }

  // 设置默认速度档位
  if (!strcmp(kw, "SPEED")) {
    if (n >= 2) {
      defaultSpeed = constrain(atoi(tok[1]), 1, 100);
      replyOK();
    } else {
      replyErr();
    }
    return;
  }

  // 状态查询
  if (!strcmp(kw, "STATUS") || !strcmp(kw, "PING")) {
    if (!robot_enabled || sitting_down) Serial1.print("SITTING\r\n");
    else Serial1.print("STANDING\r\n");
    return;
  }

  // 未知命令
  replyErr();
}

/************ 公开函数 *************/
void serialCommandInit() {
  Serial1.begin(CTRL_BAUD, SERIAL_8N1, CTRL_RX_PIN, CTRL_TX_PIN);
  bufIdx = 0;
  handshakeDone = false;
  lastReadyMs = 0;
}

void serialCommandSendReady() {
  Serial1.print("READY\r\n");
  lastReadyMs = millis();
}

void serialCommandProcess() {
  // 跳跃触发只保留一个循环周期，之后清零，避免重复起跳
  if (pendingJumpClear) {
    wrobot.jump = 0;
    pendingJumpClear = false;
  }

  // 未握手时，周期性广播 READY，便于控制板随时接入
  if (!handshakeDone && (millis() - lastReadyMs >= READY_INTERVAL_MS)) {
    Serial1.print("READY\r\n");
    lastReadyMs = millis();
  }

  // 读取并解析串口
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (bufIdx > 0) {
        cmdBuf[bufIdx] = '\0';
        handleCommand(cmdBuf);
        bufIdx = 0;
      }
    } else if (bufIdx < sizeof(cmdBuf) - 1) {
      cmdBuf[bufIdx++] = toupper((unsigned char)c);
    }
  }

  // 定时自动停止
  if (timedActive && (millis() - timedStartMs >= timedDurMs)) {
    stopMove();
  }
}
