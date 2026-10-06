#ifndef RobotActions_h
#define RobotActions_h

#include <Arduino.h>

// 单个动作步骤结构体
typedef struct {
  int joyx;          // 左右转向（-100~100），正面看，正数：向左转，负数，向右转
  int joyy;          // 前后移动（-100~100）
  int jump;          // 跳跃标记（0=不跳，1=跳）
  float roll_adjust; // 左右平衡调整（-5~5），基数0，正面看，正数：向右摆动；负数：向左摆动
  float leg_height_base; // 腿部基准高度（20~60，核心高度控制参数）,基数20，变小升高，变大降低
  unsigned long stepDuration; // 步骤持续时间（毫秒）
  unsigned long pauseDuration; // 步骤执行后的停顿时间（毫秒）
  // 舵机参数：无需默认值，动作集定义时按需填写，不填则用结构体默认值（或省略）
  int acc0;    // 舵机1加速度 0-250
  int acc1;    // 舵机2加速度 0-250
  int speed0;  // 舵机1速度 0-500
  int speed1;  // 舵机2速度 0-500
} ActionStep;

// 结束标记  后4个舵机参数，使用默认值-1
const ActionStep END_MARKER = {0, 0, 0, 0, 0, 0, 0, -1, -1, -1, -1};

// 连续动作指令集结构体
typedef struct {
  String actionName;   // 动作名称
  ActionStep* steps;   // 步骤数组（末尾必须包含END_MARKER）
} RobotAction;

// 宏：初始化RobotAction
#define CREATE_ACTION(actionName, stepsArray) \
  { actionName, stepsArray }

// -------------------------- 自定义连续动作指令集 --------------------------

ActionStep step_say_hello[] = {
  // {左+右-，前+后-，跳跃，平衡20左-右+，
  // 高度20升-降+，持续时间, 停顿时间0，
// 舵机1加速度，舵机2加速度，舵机1速度，舵机2速度}
  {0, 0, 0, 25, 10, 500, 0, -1, -1, -1, -1}, // 右摆
  {0, 0, 0, 0, -10, 200, 0, -1, -1, -1, -1}, // 回到中间最高位
  {0, 0, 0, -25, 10, 500, 0, -1, -1, -1, -1}, // 左摆
  {0, 0, 0, 0, -10, 200, 0, -1, -1, -1, -1}, // 回到中间最高位
  
  {0, 0, 0, 25, 10, 500, 0, -1, -1, -1, -1}, // 右摆
  {0, 0, 0, 0, -10, 200, 0, -1, -1, -1, -1}, // 回到中间最高位
  {0, 0, 0, -25, 10, 500, 0, -1, -1, -1, -1}, // 左摆
  {0, 0, 0, 0, 20, 500, 0, -1, -1, -1, -1}, // 回到中间最高位

  {0, 0, 0, 0, 40, 150, 0, 250, 250, 800, 800},  // 下降
  {0, 0, 0, 0, 20, 150, 300, 150, 150, 400, 400}, // 回到高度20
  {0, 0, 0, 0, 40, 150, 0, 250, 250, 800, 800},  // 下降
  {0, 0, 0, 0, 20, 150, 0, 150, 150, 400, 400}, // 回到高度20

  {0, 0, 0, 0, 20, 200, 0, -1, -1, -1, -1}, // 回到中间中位
  END_MARKER                       // 结束标记
};
RobotAction action_say_hello = CREATE_ACTION("action_say_hello", step_say_hello);

ActionStep step_down[] = {
  // {左+右-，前+后-，跳跃，平衡20左-右+，高度20升-降+，持续时间, 停顿时间0，
  // 舵机1加速度，舵机2加速度，舵机1速度，舵机2速度}
  {0, 0, 0, 0, 40, 150, 0, 250, 250, 1500, 1500},  // 下降
  {0, 0, 0, 0, 20, 150, 0, 250, 250, 800, 800}, // 回到高度20
  END_MARKER // 结束标记
};
RobotAction action_step_down = CREATE_ACTION("action_step_down", step_down);


ActionStep step_up[] = {
  // {左+右-，前+后-，跳跃，平衡20左-右+，高度20升-降+，持续时间, 停顿时间0，
  // 舵机1加速度，舵机2加速度，舵机1速度，舵机2速度}
  {0, 0, 0, 0, -20, 150, 0, 250, 250, 1500, 1500},  // 上升
  {0, 0, 0, 0, 20, 200, 0, 250, 250, 800, 800}, // 回到高度20
  END_MARKER                       // 结束标记
};
RobotAction action_step_up = CREATE_ACTION("action_step_up", step_up);

ActionStep step_right[] = {
  // {左+右-，前+后-，跳跃，平衡20左-右+，高度20升-降+，持续时间, 停顿时间0，
  // 舵机1加速度，舵机2加速度，舵机1速度，舵机2速度}
  {0, 0, 0, 50, 10, 150, 0, 250, 250, 1500, 1500}, // 右摆
  {0, 0, 0, 0, -10, 150, 0, 250, 250, 800, 800}, // 回到中间最高位
  END_MARKER                       // 结束标记
};
RobotAction action_step_right = CREATE_ACTION("action_step_right", step_right);

ActionStep step_left[] = {
  // {左+右-，前+后-，跳跃，平衡20左-右+，高度20升-降+，持续时间, 停顿时间0，
  // 舵机1加速度，舵机2加速度，舵机1速度，舵机2速度}
  {0, 0, 0, -50, 10, 150, 0, 250, 250, 1500, 1500}, // 左摆
  {0, 0, 0, 0, -10, 150, 0,250, 250, 800, 800}, // 回到中间最高位
  END_MARKER                       // 结束标记
};
RobotAction action_step_left = CREATE_ACTION("action_step_left", step_left);

#endif