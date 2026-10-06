#include "SerialParser.h"  // 仅包含自己的头文件，无需 main.h

// -------------------------- 全局变量定义（与 main.cpp 中原来的定义一致）--------------------------
char serial_buf[32];  // 存储串口接收数据
uint8_t buf_idx = 0;  // 缓冲区索引

// -------------------------- 串口接收处理函数（原逻辑不变）--------------------------
void serialReceiveProcess() {
  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n' || c == '\r') {  // 指令结束符（回车/换行）
      serial_buf[buf_idx] = '\0';  // 字符串终止
      parseSingleParam(serial_buf); // 调用解析函数
      buf_idx = 0;                 // 重置缓冲区索引
    } else if (buf_idx < 31) {     // 防止缓冲区溢出（最多存31个字符）
      serial_buf[buf_idx++] = c;   // 存入缓冲区
    }
  }
}

// -------------------------- 参数解析函数（原逻辑不变）--------------------------
void parseSingleParam(char* cmd) {
  char* eq_pos = strchr(cmd, '=');  // 找到等号位置
  if (eq_pos == NULL) {             // 无等号则忽略
    Serial.println("格式错误！需含\"=\"");
    return;
  }
  *eq_pos = '\0';  // 分割参数名与数值（cmd=参数名，eq_pos+1=数值）
  char* param_name = cmd;
  float param_val = atof(eq_pos + 1);  // 转为浮点值

  // 匹配参数名并赋值（访问全局对象，已在 SerialParser.h 中声明）
  if (strcmp(param_name, "kp") == 0) {
    pid_speed.P = param_val;
  } else if (strcmp(param_name, "ki") == 0) {
    pid_speed.I = param_val;
  } else if (strcmp(param_name, "kd") == 0) {
    pid_speed.D = param_val;

  } else if (strcmp(param_name, "zaccel_threshold") == 0) {
    zaccel_threshold = param_val;

  } else if (strcmp(param_name, "dragon_joyx_factor") == 0) {
    dragon_joyx_factor = param_val;

  // } else if (strcmp(param_name, "go_shrink_v") == 0) {
  //   go_shrink_v = param_val;
  // } else if (strcmp(param_name, "servo_up_height") == 0) {
  //   servo_up_height = param_val;
  // } else if (strcmp(param_name, "servo_down_height") == 0) {
  //   servo_down_height = param_val;
  // } else if (strcmp(param_name, "servo_down_count_max") == 0) {
  //   servo_down_count_max = param_val;


  } else if (strcmp(param_name, "mid_obstacle_pitch") == 0) {
    mid_obstacle_pitch = param_val;
  } else if (strcmp(param_name, "mid_servo_up_height") == 0) {
    mid_servo_up_height = param_val;
  } else if (strcmp(param_name, "mid_servo_down_height") == 0) {
    mid_servo_down_height = param_val;
  } else if (strcmp(param_name, "mid_motor_target") == 0) {
    mid_motor_target = param_val;

  } else if (strcmp(param_name, "high_obstacle_pitch") == 0) {
    high_obstacle_pitch = param_val;
  } else if (strcmp(param_name, "high_servo_up_height") == 0) {
    high_servo_up_height = param_val;
  } else if (strcmp(param_name, "high_servo_down_height") == 0) {
    high_servo_down_height = param_val;
  } else if (strcmp(param_name, "high_motor_target") == 0) {
    high_motor_target = param_val;

  } else {
    Serial.println("参数名错误！");
    return;
  }

  // 反馈更新结果
  Serial.printf("更新成功：%s=%.2f\n", param_name, param_val);
}