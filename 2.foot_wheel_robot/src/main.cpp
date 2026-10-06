/* ---------------------------------
Copyright (c) 2024 Mu Shibo
Modify by Tommy 2025
------------------------------------- */
#include <Arduino.h>
#include <FastLED.h>
#include <MPU6050_tockn.h>
#include <SMS_STS.h>
#include <SimpleFOC.h>
#include <FS.h>
#include "RGBController.h"   // RGB
#include "VoltageMonitor.h"  // 电源
#include "RobotActions.h"    // 动作处理
#include "SerialParser.h"  // 包含串口解析头文件
#include "RobotState.h"      // 舵机参数 + Wrobot 结构体
#include "SerialCommand.h"   // 外接控制板串口指令

// 蓝牙手柄开关：1=启用，0=停用（默认停用；需要用手柄时改成 1）
#define ENABLE_XBOX_CONTROLLER 0
#if ENABLE_XBOX_CONTROLLER
#include "XboxBluetooth.h"   // 蓝牙手柄
#endif


/************函数申明*************/

// 平衡算法函数
void lqr_balance_loop();
void yaw_loop();
void leg_loop();
void jump_loop();

void cancel_super_mode();

// 跨越障碍函数
void obstacle_detect(); // 越障高度检测
void obstacle_loop(); // 越障总函数
void obstacle_cancel();

// 常规运动控制
void resetZeroPoint();
void jump_charge(); // 腿部弯曲（蓄能）

// 动作处理函数
#if ENABLE_XBOX_CONTROLLER
void processControllerData(const XboxControllerNotificationParser& data);
#endif
void runActionSequence();


const int deadZone = 10; // 摇杆死区阈值
int trigRT = 0;
int last_trigRT = 0;
bool last_btnA = 0;
bool last_btnB = 0;
bool last_btnX = 0;
bool last_btnY = 0;
bool last_btnRS = 0;
bool last_btnLS = 0;
bool last_btnLB = 0;
bool last_btnRB = 0;
bool last_btnDirRight = 0;
bool last_btnDirLeft = 0;
bool last_btnDirUp = 0;
bool last_btnDirDown = 0;
bool last_btnSelect = 0;
bool last_btnStart = 0;
bool last_btnShare = 0;
bool last_joyRHori = 0;
bool last_joyLHori = 0;


/************实例定义*************/

// 电机实例
BLDCMotor motor1 = BLDCMotor(7);
BLDCMotor motor2 = BLDCMotor(7);
BLDCDriver3PWM driver1 = BLDCDriver3PWM(32, 33, 25, 22);
BLDCDriver3PWM driver2 = BLDCDriver3PWM(26, 27, 14, 12);

// 编码器实例
TwoWire I2Cone = TwoWire(0);
TwoWire I2Ctwo = TwoWire(1);
MagneticSensorI2C sensor1 = MagneticSensorI2C(AS5600_I2C);
MagneticSensorI2C sensor2 = MagneticSensorI2C(AS5600_I2C);

// PID控制器实例
PIDController pid_pitch(1, 0, 0, 100000, 8); // 比例系数（P）, 积分系数（I）, 微分系数（D）, 积分限幅(当I=0,无效), 输出限幅（8V防止电机过载）
PIDController pid_gyro(0.06, 0, 0, 100000, 8); 
PIDController pid_distance(0.5, 0, 0, 100000, 8);
PIDController pid_speed(0.7, 0, 0, 100000, 8);
PIDController pid_yaw_angle(1.0, 0, 0, 100000, 8);
PIDController pid_yaw_gyro(0.04, 0, 0, 100000, 8);
PIDController pid_lqr_u(1, 15, 0, 100000, 8);
PIDController pid_zeropoint(0.002, 0, 0, 100000, 4);
PIDController pid_roll_angle(8, 0, 0, 100000, 450);

// 低通滤波器实例
LowPassFilter lpf_joyy(0.2);  // 新输入值占输出值的10%，历史值占90%
LowPassFilter lpf_zeropoint(0.1);
LowPassFilter lpf_roll(0.3);
LowPassFilter lpf_height(0.1);  

// STS舵机实例
SMS_STS sms_sts;

// MPU6050实例
MPU6050 mpu6050(I2Ctwo);


/************参数定义*************/
#define pi 3.1415927

// 重力感应z轴加速度判断
float zaccel_threshold = 1.8f; // 常规重力感应阈值
float normal_zaccel_threshold = 1.8f; // 常规重力感应阈值
float super_zaccel_threshold = 1.2f; //超平衡模式下阈值
// 快速下坠加速度阈值
bool is_falling = false; // 正在下降
unsigned long last_falling_trigger_time = 0; // 上一次触发is_falling的时间戳（毫秒）
const unsigned long FALLING_COOLDOWN = 3000; // 冷却时间：3000毫秒（3秒）

// 机身平衡角度调整 roll轴
float roll_adjust = 0.0f;  
float original_roll_adjust = roll_adjust;  

// 机身高度
float leg_height_base = 20.0f;  // 改为全局变量，初始值20 腿部默认高度基准值，值越小，机身越高， 实测范围 -10（最高），52（最低）
float original_leg_height_base = leg_height_base ; 

int left_height;
int right_height;
float leg_position_add = 0;    // roll轴平衡控制量
float LegHeightDiff = 0; // 左右腿高度差

// 舵机运动参数
byte ID[2] = {1, 2};  
s16 Position[2];
u16 Speed[2];
byte ACC[2];

// 腿部高度逻辑
// float leg_height_base 20 # 基准值
// float LegHeightDiff ;  # 根据前后转向运动时，智能计算两腿高度差
// left_height，right_height  = wrobot.height + LegHeightDiff  # 根据前后左右智能计算两腿高度差
// leg_position_add = pid_roll_angle(lpf_roll(roll_angle)); 根据roll自动计算
// Position[0] 2047 + 12 + 8.4 * (left_height - leg_height_base) - leg_position_add;
// Position[1] 2047 - 12 - 8.4 * (right_height - leg_height_base) - leg_position_add;
// Position 2047 - 12 - 8.4 * (right_height                              - leg_height_base) - leg_position_add;
// Position 2047 - 12 - 8.4 * (wrobot.height + LegHeightDiff  - leg_height_base) -  pid_roll_angle(lpf_roll(roll_angle));
//                              遥控器          两腿高度差             基准值               roll轴平衡控制量

// LQR自平衡控制器参数
float LQR_angle = 0;
float LQR_gyro = 0;
float LQR_gyroZ = 0;
float LQR_speed = 0;
float LQR_distance = 0;
float angle_control = 0;
float gyro_control = 0;
float speed_control = 0;
float distance_control = 0;
float LQR_u = 0;
float pitch_zeropoint = 2;  // 2 设定默认俯仰角度，向前到正，向后倒负
float original_pitch_zeropoint = pitch_zeropoint; // 保存原始的角度零点
float distance_zeropoint =  0.5f; // 轮部位移零点偏置
float pitch_adjust = 0.0f; // 俯仰角度调整,负数前倾，正数后倾

// YAW轴控制数据
float YAW_gyro = 0;
float YAW_angle = 0;
float YAW_angle_last = 0;
float YAW_angle_total = 0;
float YAW_angle_zero_point = -10;
float YAW_output = 0;

// 记录轮部转速，用于判断跳跃状态
unsigned long last_speed_record_time = 0;  // 上次记录转速的时间
const unsigned long SPEED_RECORD_INTERVAL = 100;  // 转速记录间隔(毫秒) ，因为时间相隔太近速度差不明显的
float last_lqr_speed = 0;    // 记录上一时刻的轮部转速
float robot_speed_diff = 0;    // 速度差

// 跳跃相关参数
int jump_flag = 0; // 跳跃过程计数
int jump_pre_flag = 0; // 跳跃前蓄能计数

int place_jump_flag = 0; // 原地跳跃
int forward_jump_flag = 0; // 前跳跃标志位
int back_jump_flag = 0; // 后跳跃标志位
int left_jump_flag = 0; // 左跳跃标志位
int right_jump_flag = 0; // 右跳跃标志位

// 越障模式
String obstacle_status = "";
String obstacle_level = ""; // 越障高度，low/mid/high

bool obstacle_mode = false; // 越障模式
unsigned long obstacle_btn_down = 0; // 0=不开启 >0开启

int obstacle_enconter_count = 0; // 遇到障碍计数
int obstacle_pitch_adjust_count = 0; // 前倾控制
int obstacle_motor_go_count = 0; // 电机前进计数

float obstacle_pitch =  20.0f;
int servo_up_height = 0; 
int servo_down_height = 50; 
float motor_target = -3.0f;

 // 蓝灯，最低障碍
float low_obstacle_pitch = 21.0f;
int low_servo_up_height = 0;
int low_servo_down_height = 30; 
float low_motor_target = -4.0f;

 // 黄灯，中度障碍
float mid_obstacle_pitch = 25.0f;
int mid_servo_up_height = 0;
int mid_servo_down_height = 30; 
float mid_motor_target = -8.0f;

// 红灯，最高障碍
float high_obstacle_pitch = 30.0f;
int high_servo_up_height = 380; // +390=MAX
int high_servo_down_height = 0; 
float high_motor_target = -9.0f;

// 超级平衡模式参数
bool super_balance_mode = false; // 超级平衡模式
PIDController pid_super_balance(11.5, 0, 0, 100000, 8); // 适配joyy实际需求

// 舵机中位校准
int calibrationOfs_flag = 0;

// 倒地恢复计数
int fallRecoveryResetCounter = 0;

// 快速下坠状态标志

// 开机默认坐下 修改
bool robot_enabled = false; // 开机禁用平衡，默认坐下
bool last_robot_enabled = false; // 同步初始状态
int sitting_down = 1; // 开机直接进入“坐下状态”，避免无控制
int stand_up_count = 0; // 站立标志位

// 动作组执行状态管理
bool isExecutingAction = false;
bool isPausing = false;                
unsigned long currentPauseDuration = 0; 
unsigned long stepStartTime = 0;
unsigned long pauseStartTime = 0;    
int currentStepIndex = 0;
RobotAction* currentAction = NULL;

//  龙头模式
bool dragon_head_mode = false; // 龙头模式标志
float dragon_joyx_factor = 1.0f;
int convertedJ1PotX;
int convertedJ2PotX;
int last_convertedJ1PotX;
int last_convertedJ2PotX;

Wrobot wrobot;


void setup()
{
  Serial.begin(115200);   // 通讯串口
  while (!Serial);     // 等待串口连接（USB串口需此逻辑）
  Serial.println("参数调节就绪，格式：Kp=值;");

  Serial2.begin(1000000); // 腿部sts舵机

  // 外接控制板串口初始化（RX=IO4, TX=IO15）
  serialCommandInit();

  // 舵机初始化
  sms_sts.pSerial = &Serial2;

  ID[0] = 1;
  ID[1] = 2;
  ACC[0] = 30;
  ACC[1] = 30;
  Speed[0] = 400;
  Speed[1] = 400;
  Position[0] = SERVO0_MIN;
  Position[1] = SERVO1_MIN;
  sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);

  // 电压检测
  adc_calibration_init();
  adc1_config_width(width);
  adc1_config_channel_atten(channel, atten);
  esp_adc_cal_characterize(unit, atten, width, 0, &adc_chars);

  // 电量显示LED
  pinMode(LED_BAT, OUTPUT);

  // 编码器设置
  I2Cone.begin(19, 18, 400000UL);
  I2Ctwo.begin(23, 5, 400000UL);
  sensor1.init(&I2Cone);
  sensor2.init(&I2Ctwo);

  // mpu6050设置
  mpu6050.begin();
  mpu6050.calcGyroOffsets(true);

  // 连接motor对象与编码器对象
  motor1.linkSensor(&sensor1);
  motor2.linkSensor(&sensor2);

  // 速度环PID参数
  motor1.PID_velocity.P = 0.05;
  motor1.PID_velocity.I = 1;
  motor1.PID_velocity.D = 0;

  motor2.PID_velocity.P = 0.05;
  motor2.PID_velocity.I = 1;
  motor2.PID_velocity.D = 0;

  // 驱动器设置
  motor1.voltage_sensor_align = 6;
  motor2.voltage_sensor_align = 6;
  driver1.voltage_power_supply = 8;
  driver2.voltage_power_supply = 8;
  driver1.init();
  driver2.init();

  // 连接motor对象与驱动器对象
  motor1.linkDriver(&driver1);
  motor2.linkDriver(&driver2);

  motor1.torque_controller = TorqueControlType::voltage; // 扭矩控制器类型为 "电压模式"
  motor2.torque_controller = TorqueControlType::voltage;
  motor1.controller = MotionControlType::torque; // 运动控制器类型为 "扭矩模式"
  motor2.controller = MotionControlType::torque;
    
  // monitor相关设置
  motor1.useMonitoring(Serial);
  motor2.useMonitoring(Serial);
  
  // 电机初始化
  motor1.init();
  motor1.initFOC();
  motor2.init();
  motor2.initFOC();

  // RGB初始化
  initLEDs();

#if ENABLE_XBOX_CONTROLLER
  // Xbox手柄初始化
  xboxControllerInit();
#endif

  // 初始化完成，向控制板广播 READY
  serialCommandSendReady();

  delay(500);
}

void loop()
{
 serialReceiveProcess(); // 调用串口接收函数（核心修改）
  serialCommandProcess(); // 外接控制板串口指令

  handleLEDBlink();       // 处理LED闪烁
#if ENABLE_XBOX_CONTROLLER
  process_xbox_notif();   // 处理xbox手柄通知
  updateXboxVibration();  // 手柄震动状态
#endif
  
  bat_check();            // 电压检测
  delayMicroseconds(1500);
  mpu6050.update();       // IMU数据更新

  lqr_balance_loop();     // lqr自平衡控制

  yaw_loop();             // yaw轴转向控制

  obstacle_detect();        // 障碍检测

  obstacle_loop();  // 越障跳跃控制

  jump_loop();            // 跳跃动作

  leg_loop();             // 腿部动作控制
  
  runActionSequence();    // 执行动作序列 


  // 记录上一次的遥控数据数据
  wrobot.joyx_last = wrobot.joyx;
  wrobot.joyy_last = wrobot.joyy;

  motor1.target = (-0.5) * (LQR_u + YAW_output); // target正数小车向前，负数向后
  motor2.target = (-0.5) * (LQR_u - YAW_output);

  // fallRecoveryResetCounter作用：防止小车倒地后某个角度，陷入循环，motor.target一直=0
  // 向后倒地-31度，向前倒地（带支架）52度
  if ( ( 
          (LQR_angle < -30.0f  && fallRecoveryResetCounter < 130) // 向后倒地
          || (LQR_angle > 35.0f && fallRecoveryResetCounter < 400) // 向前倒底 实测角度小于52度，并且完全倒地耗时较长
        )   
     && !sitting_down && robot_enabled && wrobot.joyy == 0) {
          resetZeroPoint();
          fallRecoveryResetCounter++;
          motor1.target = 0;
          motor2.target = 0; 
  }

  // 大仰角倒地，轮子就不要转了 修改
  if (abs(LQR_angle) > 120.0f) {
      motor1.target = 0;
      motor2.target = 0;
  }

  // 达到平衡角度后，重置重置计数器
  if( abs(LQR_angle) < 25.0f){
    fallRecoveryResetCounter = 0;
  }
  // Serial.println(String(LQR_angle));

  // 向左跳跃
  if ( left_jump_flag > 2) {
    motor1.target = 5.0; // 脉冲
    motor2.target = -5.0;
  } 

  // 向右跳跃
  if ( right_jump_flag > 2) {
    motor1.target = -5.0; // 脉冲
    motor2.target = 5.0;
  } 

  // 向前跳跃
  if ( forward_jump_flag > 2) {
    motor1.target = 2.0; // 脉冲
    motor2.target = 2.0;
  } 

  // 向后跳跃
  if ( back_jump_flag > 2 ) {
    motor1.target = -0.8; // 脉冲
    motor2.target = -0.8;
  } 

  // 原地跳越
  if ( place_jump_flag > 2) {
    motor1.target = 1.5; // 脉冲
    motor2.target = 1.5;
  } 

  // 站立计数
  if( stand_up_count > 0){
    stand_up_count ++;
  }
  if (stand_up_count > 500){
    stand_up_count = 0;
  }
  
  // 越障跳跃控制
  if(obstacle_enconter_count){

    // 监测前倾角度，进入motor_goforward
    if(obstacle_status == "pitch_adjust" && LQR_angle > obstacle_pitch){ 
       obstacle_status = "pitch_adjust_over";
    }

    // 向前倾倒过程
    if(obstacle_status == "pitch_adjust" ){
       motor1.target = motor2.target = -0.2f;
    }

    // 控制电机速度
    if(obstacle_status == "motor_go"){
      motor1.target = motor2.target = motor_target; 
    }

  }


  // 小车坐下
  if (sitting_down) {

    // 关闭超级平衡模式
    cancel_super_mode();

    sitting_down ++;

    if (sitting_down < 15) {
      // 如果遥感向后运动，给个向后退脉冲，让小车向前倒地
      if(wrobot.joyy > 0){
        motor1.target = 1.0;  // 向前倒下
        motor2.target = 1.0;
        Position[0] = SERVO0_MIN + 60; 
        Position[1] = SERVO1_MIN - 60; 
      } else { // 否则默认向后倒地 给个向后脉冲
        motor1.target = -1.0;  // 向后倒下
        motor2.target = -1.0;
        Position[0] = SERVO0_MIN; 
        Position[1] = SERVO1_MIN;
      }

    }else{  // 通过简单的开环控制前后左右
      float speed_coeff = 0.04; // 速度系数：值越大，移动越快
      motor1.target = (-0.5) * (wrobot.joyy +  wrobot.joyx) * speed_coeff; 
      motor2.target = (-0.5) * (wrobot.joyy -  wrobot.joyx) * speed_coeff;

    }

    // 收缩双腿，舵机到最低位置
    if (sitting_down == 5) {
      ACC[0] = ACC[1] = 150;
      Speed[0] = Speed[1] = 400;
      sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
    }
   
  }

  // 迭代计算FOC相电压
  motor1.loopFOC();
  motor2.loopFOC();

  // 设置轮部电机输出
  motor1.move();
  motor2.move();

} // loop()

// lqr自平衡控制
void lqr_balance_loop()
{
  /*
  motor.target 负数小车向后，正数小车向前
  LQR_u 越大，电机输出的转矩越大，电机向后转，默认0-8V
  LQR_u = angle_control + gyro_control + distance_control + speed_control
          = 俯仰角度控制 + 俯仰角速度控制 + 距离控制 + 速度控制

    angle_control = pid_pitch(  LQR_angle       -   pitch_zeropoint) // 实际pitch角度-目标pitch角度
                                LQR_angle = mpu6050.getAngleY() 
    
    gyro_control = pid_gyro(  LQR_gyro - 0   ) // 实际角速度，目标值是零
                              LQR_gyro =  mpu6050.getGyroY()
    
    distance_control = pid_distance(  LQR_distance - distance_zeropoint) // 两个电机平均旋转弧度（实际位移量） - 位移基准零点（某一时刻LQR_distance快照）
                                      LQR_distance = (-0.5)*(motor1.shaft_angle + motor2.shaft_angle)  

    speed_control = pid_speed(LQR_speed - speed_target_coeff * lpf_joyy(wrobot.joyy)) // 两个电机平均速度（实际速度） - 摇杆前后输入值* 系数 ，就速度差
                              LQR_speed = (-0.5) * (motor1.shaft_velocity + motor2.shaft_velocity) 
                              speed_target_coeff = 前进后退系数
  yaw 偏航角控制
  float yaw_angle_control = pid_yaw_angle(yaw_target); 
                                          yaw_target = wrobot.joyx * 0.1; // 使用摇杆左右转向值

  float yaw_gyro_control = pid_yaw_gyro(YAW_gyro); // yaw 轴的角速度控制
                                        YAW_gyro =  (float)mpu6050.getGyroZ();
  
  YAW_output = yaw_angle_control + yaw_gyro_control;

  motor1.target = (-0.5) * (LQR_u + YAW_output); // target正数小车向前，负数向后
  motor2.target = (-0.5) * (LQR_u - YAW_output);

   */

  LQR_distance = (-0.5) * (motor1.shaft_angle + motor2.shaft_angle); // 两个电机的旋转角度（shaft_angle）,单位：弧度（rad）实际位移量
  LQR_speed = (-0.5) * (motor1.shaft_velocity + motor2.shaft_velocity); // 两个电机角速度（shaft_velocity）,单位：弧度 / 秒（rad/s）
  LQR_angle = (float)mpu6050.getAngleY(); // mpu6050 pitch 角度，单位：度（°）
  
  LQR_gyro = (float)mpu6050.getGyroY(); // pitch Y轴角速度,单位：度 / 秒（°/s）

  angle_control = pid_pitch(LQR_angle - pitch_zeropoint) + pitch_adjust; //  
  
  gyro_control = pid_gyro(LQR_gyro);

  // 前进后退跳跃的系数都不一样
  float speed_target_coeff = 0.1; 
  if( jump_flag ){ // 跳跃
    speed_target_coeff = 0.1;
  }else if(wrobot.joyy > 0 ){ // 前进
    speed_target_coeff = 0.18;
  }else if(wrobot.joyy < 0){ // 后退 
    speed_target_coeff = 0.11;
  }else{
    speed_target_coeff = 0.1;
  }
 
  // 超级平衡模式 误差=实际角度 - 默认零点
  if (super_balance_mode && !is_falling && robot_enabled && !sitting_down && !jump_flag ) {
    float balance_joyy = pid_super_balance(LQR_angle -pitch_zeropoint);
    balance_joyy = constrain(balance_joyy, -100.0f, 100.0f); 
    wrobot.joyy = balance_joyy;
  }

  speed_control = pid_speed(LQR_speed - speed_target_coeff * lpf_joyy(wrobot.joyy));  // 最大8v

  // 检测轮子差速，判断轮子是否离地
  unsigned long current_time = millis();
  if (current_time - last_speed_record_time >= SPEED_RECORD_INTERVAL) {
    robot_speed_diff = LQR_speed - last_lqr_speed;

    if(robot_speed_diff > 18.0) // 轮子离地
    {
      // wheel_ground_flag = 0; // 轮子离地标记
      //  Serial.println("TAKE OFF");
      // Serial.print(wheel_ground_flag);
      // Serial.print(",robot_speed_diff:");
      // Serial.println(robot_speed_diff);
      // Serial.print(",jump_flag:");
      // Serial.println(jump_flag); // 100 左右
    }
    if( robot_speed_diff < -9.0) // 轮子着地
    {
      // wheel_ground_flag = 1; // 轮子着地标记
      // Serial.println("LANDING");
      // Serial.print(",robot_speed_diff:");
      // Serial.println(robot_speed_diff);
      // Serial.print(",jump_flag:");
      // Serial.println(jump_flag); // 120左右
      if( jump_flag ){ // 落地点作为新的位移零点
         resetZeroPoint(); 
      }
      if( jump_flag == 0){
#if ENABLE_XBOX_CONTROLLER
        XboxKeyVibration();  // 大减速运动状态手柄震动
#endif
      }
    }
    last_lqr_speed = LQR_speed;  // 每隔100ms更新一次历史转速
    last_speed_record_time = current_time;  // 更新记录时间
  }
   
  // 重置位移零点和积分情形
  // 1.判断摇杆是否没有前后左右运动指令
  if ((wrobot.joyx_last != 0 && wrobot.joyx == 0) || (wrobot.joyy_last != 0 && wrobot.joyy == 0)) 
  {
    resetZeroPoint(); 
  }
  
  // 2. 运动中实时重置位移零点和积分情形
  if (abs(robot_speed_diff) > 10 || (abs(LQR_speed) > 15) // 这两种是启动后自平衡，速度较快
     || wrobot.joyy != 0 || sitting_down ) 
  {
       resetZeroPoint(); 
  }

  distance_control = pid_distance(LQR_distance - distance_zeropoint);

  // 计算 LQR_u
  // 必须是在跳跃时jump_flag > 0，LQR_u= 角度控制量 + 角速度控制量
  if ((jump_flag > 0 and jump_flag < 120)) // jump_flag：100离地 120着地
  {        
    LQR_u = angle_control + gyro_control;  // 角度控制量 + 角速度控制，位移和速度控制分量被忽略
  }
  else // 当轮部未离地时，LQR_u：4个参数
  {
    // 当轮部未离地时，LQR_u =角度控制量+角速度控制量+位移控制量+速度控制量
    LQR_u = angle_control + gyro_control + distance_control + speed_control;
  }

  // 小车没有控制的时候自稳定状态
  // 控制量lqr_u<5V，前进后退控制量很小， 遥控器无信号输入joyy=0，轮部位移控制正常介入distance_control<4，不处于跳跃后的恢复时期jump_flag=0,以及不是坐下状态
  if (abs(LQR_u) < 5 && wrobot.joyy == 0 && abs(distance_control) < 4 && (jump_flag == 0)) //  && !sitting_down
  {
    LQR_u = pid_lqr_u(LQR_u); // 小转矩非线性补偿
    pitch_zeropoint -= pid_zeropoint(lpf_zeropoint(distance_control)); // 重心自适应
  }
  else
  {
    pid_lqr_u.error_prev = 0; // 输出积分清零
  }

  if(jump_flag){
    pid_speed.P = 0.5;
  } if (wrobot.height < 50){
    pid_speed.P = 0.8;
  }else if (wrobot.height < 64){
    pid_speed.P = 0.6;
  }else{
    pid_speed.P = 0.5;
  }
  
}

// 腿部动作控制（舵机）
void leg_loop()
{
  // 如果坐下就不控制腿部
  if(sitting_down 
    || jump_flag || jump_pre_flag 
    || left_jump_flag || right_jump_flag || forward_jump_flag || back_jump_flag || place_jump_flag
    || obstacle_enconter_count
  ){ 
      return; // 跳过后续leg高度计算
  }

  // 1. 获取并处理MPU6050 Z轴加速度
  float z_accel = mpu6050.getAccZ(); // 1.1~1.2左右

  // Serial.print("z_accel:");
  // Serial.println(z_accel);

  // 2 触发重力感应-向下
  if (z_accel > zaccel_threshold  && !is_falling 
    && stand_up_count == 0
    && roll_adjust == original_roll_adjust
    && leg_height_base == original_leg_height_base
    && !obstacle_btn_down
    && !obstacle_enconter_count 
    && (millis() - last_falling_trigger_time > FALLING_COOLDOWN) // 核心：5秒冷却
  ) {
    is_falling = true;
    last_falling_trigger_time = millis(); // 更新触发时间戳
    // 腿部收缩
    ACC[0] = ACC[1] = 250;
    Speed[0] = Speed[1] = 1000;
    Position[0] = SERVO0_MIN + 150;
    Position[1] = SERVO1_MIN - 150;
    sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
    resetZeroPoint();
    return; // 跳过后续正常高度计算
  }

  //  下坠重力感应后舵机结束判断与恢复逻辑
  if (is_falling) {
    if (!sms_sts.ReadMove(ID[0]) && !sms_sts.ReadMove(ID[1])) {
      is_falling = false;
    }else{
       return;
    }
  }

  // 机身大角度倒地时，强制腿部收缩
  if (abs(LQR_angle) > 25.0f) 
  {
    // 默认舵机速度和加速度
    ACC[0] = SERVO0_ACC;
    ACC[1] = SERVO1_ACC;
    Speed[0] = SERVO0_SPEED;
    Speed[1] = SERVO1_SPEED;
    Position[0] = SERVO0_MIN;
    Position[1] = SERVO1_MIN;
    sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
    return; // 跳过后续leg高度计算
  }

   // 正常腿部高度计算
  ACC[0] = wrobot.acc0;    // 强转为byte（舵机库要求）
  ACC[1] = wrobot.acc1;
  Speed[0] = wrobot.speed0; // 强转为u16（舵机库要求）
  Speed[1] = wrobot.speed1;

  float roll_angle = (float)mpu6050.getAngleX() + roll_adjust; // 加上roll角度偏差
  leg_position_add = pid_roll_angle(lpf_roll(roll_angle));

  // 左后腿默认等于 wrobot.height
  left_height = wrobot.height;
  right_height = wrobot.height;
  LegHeightDiff = 0; // 初始化 roll 角度补偿值

  // 智能计算左右脚高度差， 如果同时有转向和前后指令 ，自动计算腿部高度补偿
    if (abs(wrobot.joyx) > 10 && abs(wrobot.joyy) > 10) 
    {
      LegHeightDiff = map(abs(wrobot.joyy), 0, 100, 0, 10); 
      leg_position_add = 0; // 不需要两个脚一样高
    }
    if (wrobot.joyx > 10  && abs(wrobot.joyy) > 10) 
    {
      // 左脚抬高
      left_height = wrobot.height + LegHeightDiff ;
      // Serial.print("左脚抬高");
    } 
    else if (wrobot.joyx < -10  && abs(wrobot.joyy) > 10) 
    {
      // 右脚抬高
      right_height = wrobot.height + LegHeightDiff;
      // Serial.print("右脚抬高");
    } 
  
  // 根据新的高度计算舵机位置
  // id[0] 正面看右手，id[1] 正面看左手
  // 2047是舵机中间位置，8.4是系数，32是默认高度，最高80，最低32
  // const int LEG_HEIGHT_BASE   这值越小，机身越高，实测范围 0（最高），52（最低）
  Position[0] = 2047  + 8.4 * (left_height - leg_height_base ) - leg_position_add;
  Position[1] = 2047  - 8.4 * (right_height - leg_height_base ) - leg_position_add;

  // 舵机1：限制在 SERVO0_MIN（下限）~ SERVO0_MAX（上限），和原逻辑一致
  Position[0] = constrain(Position[0], SERVO0_MIN, SERVO0_MAX);
  // 舵机2：限制在 SERVO1_MAX（下限）~ SERVO1_MIN（上限），交换参数顺序适配反向范围
  Position[1] = constrain(Position[1], SERVO1_MAX, SERVO1_MIN);
  
  sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);

}

// 收缩小腿蓄能
void jump_charge() // 腿部弯曲（蓄能）
{
    ACC[0] = 250;
    ACC[1] = 250;
    Speed[0] = 450;
    Speed[1] = 450;
    Position[0] = (SERVO0_MIN  + 80); // 不能是SERVO1_MIN值，防止腿部碰撞
    Position[1] = (SERVO1_MIN  - 80); 
    sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
}

// 跳跃控制
void jump_loop()
{ 

  if ((wrobot.jump == 1) && (jump_pre_flag == 0))
  {
    jump_charge();
    jump_pre_flag =  1;
  }

  if( abs(LQR_angle) > 20.0f && jump_pre_flag ==  1){ // 车身没有平稳，不能跳跃
    wrobot.jump = 0;
    jump_pre_flag = 0; // 重置跳跃准备标志
    jump_flag = 0; // 跳跃过程结束
    left_jump_flag = 0;
    right_jump_flag = 0;
    forward_jump_flag = 0;
    place_jump_flag = 0;
    back_jump_flag = 0;
    startLEDBlink(CRGB::Yellow, 100,  2 );
    return; // 跳过后续leg高度计算
  }

  if(jump_pre_flag > 0 ){
    jump_pre_flag ++;
  }
  // 给jump_pre_flag一点余量，确保收缩小腿蓄能已经结束，且舵机停止运动了
  if(jump_pre_flag > 20  && !sms_sts.ReadMove(ID[0]) && !sms_sts.ReadMove(ID[1])){ 
      
    // 前后左右跳跃配合loop函数中脉冲
      if( left_jump_flag ){ 
        left_jump_flag ++ ;
        
        if( left_jump_flag > 20 ){ 
          left_jump_flag = 0;
        }

      }else if( right_jump_flag ){
        right_jump_flag ++ ;
        
        if( right_jump_flag > 20 ){
          right_jump_flag = 0;
        }

      }else if( forward_jump_flag ){
        forward_jump_flag ++ ;
        
        if(forward_jump_flag > 20){ // 向前跳跃只有20个脉冲
          forward_jump_flag = 0;
        }

      }else if( back_jump_flag ){
        back_jump_flag ++ ;
        
        if(back_jump_flag > 15){
          back_jump_flag = 0;
        }
      
      }else if( place_jump_flag ){ // 原地跳跃
        place_jump_flag ++ ;
        
        if(place_jump_flag > 20){
          place_jump_flag = 0;
        }


      }else{ // 跳跃开始

        jump_flag = 1;
        jump_pre_flag = 0; 

        ACC[0] = 0;
        ACC[1] = 0;
        Speed[0] = 0;
        Speed[1] = 0;
        Position[0] = SERVO0_MAX + 20; // 极限距离
        Position[1] = SERVO1_MAX - 20;
        sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
      }

  }

  if (jump_flag > 0) // 跳跃计数;
  {
    jump_flag ++;
  }

   // 跳跃中，腿部缩起来
  if( (jump_flag > 30) && (jump_flag < 35) )
  {
    ACC[0] = 0;
    ACC[1] = 0;
    Speed[0] = 0;
    Speed[1] = 0;
    Position[0] = (SERVO0_MIN  + 80); // 100 约等于食指宽度
    Position[1] = (SERVO1_MIN  - 80); 
    sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
  }

  // 轮子着地
  // if (jump_flag > 200) //  2000=6s ,1s=2000/6=333.33
  if (jump_flag > 150) //  2000=6s ,1s=2000/6=333.33
  {
    jump_flag = 0; // 跳跃过程结束
  }

}

// yaw轴转向控制 （转向控制）
void yaw_loop()
{
  // 跳跃中，YAW_output 设为0，避免干扰左右旋转
  if(jump_flag) 
  {
    YAW_output = 0;
    return;
  }

 // 1. 根据abs(wrobot.joyx)，动态设置yaw_target系数0.1~1
  float coeff = map( abs(wrobot.joyx), 0, 100, 10, 100) / 100.0f; 
  coeff = constrain(coeff, 0.1f, 1.0f); 
  float yaw_target = wrobot.joyx * coeff; 

  // 2. 根据abs(wrobot.joyx)，提高PID响应，临时放大系数
  float yaw_angle_fator = 1.0f;
  if( wrobot.joyy == 0 && abs(wrobot.joyx) > 10 && !dragon_head_mode){ // 原地打转
    yaw_angle_fator = map( abs(wrobot.joyx), 0, 100, 1.0, 2.0); 
    // 根据转向速度，智能计算wrobot.height 
    // abs(wrobot.joyx) 范围0到100，默认0，输出范围20-50,默认 30
    // 反向映射：绝对值30→60，绝对值100→0（线性过渡）
    wrobot.height = map(abs(wrobot.joyx), 0, 100, 50, 30);
    // 强制约束输出在0~60范围内（防止异常值）
    wrobot.height = constrain(wrobot.height, 30, 50);
    // 使用低波过滤器，平滑数据
    wrobot.height = lpf_height(wrobot.height);
    
    // 增加rgb效果
    startLEDBlink(CRGB::Red, 200, 1);
  }

  float yaw_angle_control = pid_yaw_angle(yaw_target) * yaw_angle_fator; 
  
  // 限制yaw_angle_control上限，避免超压（按电机7.4V反推，设为12较合适）
  yaw_angle_control = constrain(yaw_angle_control, -12.0f, 12.0f);


  YAW_gyro = (float)mpu6050.getGyroZ(); // 左右偏航角速度，用于纠正小车前后走直线时的角度偏差 
  float yaw_gyro_control = pid_yaw_gyro(YAW_gyro);
  
  YAW_output = yaw_angle_control + yaw_gyro_control;



}

// 重置距离零点
void resetZeroPoint() {
  distance_zeropoint =  LQR_distance;
  pid_lqr_u.error_prev = 0;
  pid_super_balance.error_prev = 0.0f; // 重置积分项
  pitch_adjust = 0.0f;
}

/*========================================
 * xbox 手柄控制
 * ========================================*/
#if ENABLE_XBOX_CONTROLLER
void processControllerData(const XboxControllerNotificationParser& remoteData) {

// String str = String(xboxController.xboxNotif.btnY) + "," + // Y键
//             String(xboxController.xboxNotif.btnX) + "," + // X键
//             String(xboxController.xboxNotif.btnB) + "," + // B键
//             String(xboxController.xboxNotif.btnA) + "," + // A键
//             String(xboxController.xboxNotif.btnLB) + "," + // 左肩键
//             String(xboxController.xboxNotif.btnRB) + "," + // 右肩键
//             String(xboxController.xboxNotif.btnSelect) + "," + // 中间十字 左键 试图键
//             String(xboxController.xboxNotif.btnStart) + "," + // 中间十字 右键 菜单键
//             String(xboxController.xboxNotif.btnXbox) + "," + // 中间十字 上键 home键
//             String(xboxController.xboxNotif.btnShare) + "," + // 中间十字 下键 截图键
//             String(xboxController.xboxNotif.btnLS) + "," + // 左摇杆键
//             String(xboxController.xboxNotif.btnRS) + "," + // 右摇杆键
//             String(xboxController.xboxNotif.btnDirUp) + "," + // 上键
//             String(xboxController.xboxNotif.btnDirRight) + "," + // 右键
//             String(xboxController.xboxNotif.btnDirDown) + "," + // 下键
//             String(xboxController.xboxNotif.btnDirLeft) + "," + // 左键
//             String(xboxController.xboxNotif.joyLHori) + "," + // 左摇杆 水平轴 （左-右 0 -65535）
//             String(xboxController.xboxNotif.joyLVert) + "," + // 左摇杆 垂直轴 （上-下 0 -65535）
//             String(xboxController.xboxNotif.joyRHori) + "," + // 右摇杆 水平轴 （左-右 0 -65535）
//             String(xboxController.xboxNotif.joyRVert) + "," + // 右摇杆 垂直轴 （上-下 0 -65535）
//             String(xboxController.xboxNotif.trigLT) + "," + // 左扳机键 （松开- 按下 0 -1023）
//             String(xboxController.xboxNotif.trigRT) ; // 右扳机键 （松开- 按下 0 -1023）
//   Serial.println(str);

 // 正在执行动作组，忽略其他指令
  if (isExecutingAction) {
    return; 
  }

  // 坐下
  if (remoteData.btnLB && !last_btnLB && robot_enabled) { 
    robot_enabled = false;
    sitting_down = 1; // 标记开始坐下动作保存当前的角度零点
  }
  // 第二次按坐下，让舵机失能
  if (remoteData.btnLB && !last_btnLB && !robot_enabled) { 
    sms_sts.EnableTorque(ID[0], 0);  // 第一个舵机失能 0 失能，1 使能
    sms_sts.EnableTorque(ID[1], 0);  // 第二个舵机失能
  }
   last_btnLB = remoteData.btnLB;

  // 起立
  if (remoteData.btnRB && !robot_enabled) {
    robot_enabled = true;
    sitting_down = 0; // 清除坐下动作标记
    stand_up_count ++; // 站立标志位
  }
   last_btnRB = remoteData.btnRB;

  // 修改后代码（保留摇杆指令，仅禁止起升/跳跃）
  if (!robot_enabled || sitting_down) {
    wrobot.jump = 0; // 禁止跳跃
    wrobot.roll = 0; // 禁止左右摇摆（起升相关）
    wrobot.height = 0; // 禁止高度调整（起升相关）
  }

  // 跳跃状态不接受指令
  if( jump_flag > 0){
    wrobot.jump = 0; // 强制设为“不跳跃”
    wrobot.joyx = 0; // 清空左右转向指令
    wrobot.joyy = 0; // 清空前后移动指令
    wrobot.roll = 0; // 清空左右摇摆指令
    return;
  }

    // 前后移动 右摇杆Y轴
    int convertedJ2PotY = constrain(
      map(remoteData.joyRVert, 0, 65535, 100, -100),  // 原始0~65535，目标100~-100
      -100, 100
    );
    if(abs(convertedJ2PotY) > deadZone) {    // 前后运动 -100 到 100
      wrobot.joyy = convertedJ2PotY;
    }else{
      wrobot.joyy = 0;
    } 

      // 龙头模式
    if (remoteData.btnStart && !last_btnStart) {
      dragon_head_mode = !dragon_head_mode; // 切换模式
      if (dragon_head_mode) {
        setLEDColor(CRGB::Purple); // 紫色LED标识龙头模式
      } else {
        setLEDColor(CRGB::Black);  // 关闭LED
      }
    }

    // 左右转向 左摇杆X轴：先映射到-100~100，再按比例放大到-100~100
    if(!dragon_head_mode){ // 双手控制模式
      convertedJ1PotX = constrain(
        map(remoteData.joyLHori, 0, 65535, -100, 100),  
        -100, 100
      );
      if(abs(convertedJ1PotX) > deadZone
          && ( (last_convertedJ1PotX > 0 &&  convertedJ1PotX >= last_convertedJ1PotX ) 
              || (last_convertedJ1PotX < 0 &&  convertedJ1PotX <= last_convertedJ1PotX)
          )) { // 左右运动  -100 到 100
        wrobot.joyx = convertedJ1PotX; 
      } else {
        wrobot.joyx = 0;
      }

    }else{ // 龙头模式
      convertedJ2PotX = constrain(
        map(remoteData.joyRHori, 0, 65535, -100, 100),  
        -100, 100
      );
      if(abs(convertedJ2PotX) > deadZone
      && ( (last_convertedJ2PotX > 0 &&  convertedJ2PotX >= last_convertedJ2PotX ) 
              || (last_convertedJ2PotX <0 &&  convertedJ2PotX <= last_convertedJ2PotX )
      )) { // 左右运动  -100 到 100
        wrobot.joyx = convertedJ2PotX * dragon_joyx_factor; 
      } else {
        wrobot.joyx = 0;
      }
    }

    last_convertedJ1PotX = convertedJ1PotX;
    last_convertedJ2PotX = convertedJ2PotX;

   
    // 智能计算wrobot.height ，根据前后速度，
    // convertedJ2PotY 范围-100到100，默认0，输出范围30（最矮）-50（最高）, 默认 50
    // wrobot.height 0 最矮，50最高
    wrobot.height = map( abs(convertedJ2PotY), 0, 100, 50, 30);
    wrobot.height = constrain(wrobot.height, 30, 50);
    wrobot.height = lpf_height(wrobot.height);

    // // 右摇摆
    if(remoteData.btnDirRight) { 
      // roll_adjust += 0.20;

      isExecutingAction = true;          // 标记开始执行动作
      currentAction = &action_step_right; // 指定执行的动作
      stepStartTime = millis();          // 记录第一步开始时间
      currentStepIndex = 0;              // 从第一步开始

    }
    // 左摇摆
    if(remoteData.btnDirLeft) { 
      // roll_adjust -= 0.20;
      isExecutingAction = true;          // 标记开始执行动作
      currentAction = &action_step_left; // 指定执行的动作
      stepStartTime = millis();          // 记录第一步开始时间
      currentStepIndex = 0;              // 从第一步开始
    }

    // 前倾
    // if(remoteData.btnDirRight) { 
    //   pitch_adjust += 0.20;
    // }
    // 后仰
    // if(remoteData.btnDirLeft) { 
    //   pitch_adjust -= 0.20;
    // }


    // 抬升
    if(remoteData.btnDirUp) { 
      // leg_height_base -= 0.20;
      // Serial.print("leg_height_base");
      // Serial.println(leg_height_base);

      isExecutingAction = true;          // 标记开始执行动作
      currentAction = &action_step_up; // 指定执行的动作
      stepStartTime = millis();          // 记录第一步开始时间
      currentStepIndex = 0;              // 从第一步开始

    }

    // 下降
    if(remoteData.btnDirDown) { 
      // leg_height_base += 0.20;
      // Serial.print("leg_height_base");
      // Serial.println(leg_height_base);

      isExecutingAction = true;          // 标记开始执行动作
      currentAction = &action_step_down; // 指定执行的动作
      stepStartTime = millis();          // 记录第一步开始时间
      currentStepIndex = 0;              // 从第一步开始
    }

    // 还原
    if (remoteData.btnLS) {
      roll_adjust = original_roll_adjust; 
      leg_height_base = original_leg_height_base;
      pitch_zeropoint = original_pitch_zeropoint;
      pitch_adjust = 0.0f;
    }

    //  右扳机 高度向下 ：原始0-1023
    // leg_height_base = constrain(
    //     map(remoteData.trigRT, 0, 1023, 20, 60), // 原始范围0-1023，默认0，所以leg_height_base=20
    //     20, 60
    //   );

  // 左扳机 rgb led 上键 闪烁6次：红红黄黄绿绿
  if(remoteData.trigLT && !isBlinking){
     startColorSequenceBlink();
  }

  // 跳跃 右摇杆按键
  if (remoteData.btnRS && !last_btnRS  && robot_enabled) { // 1=true，0=false
    wrobot.jump = 1; // 跳跃
    place_jump_flag = 1; // 跳跃
  }else{
    wrobot.jump = 0; // 不跳跃
  }
  last_btnRS = remoteData.btnRS; 


  // 向左跳跃
  if (remoteData.btnX && !last_btnX  && robot_enabled) { 
    wrobot.jump = 1;
    left_jump_flag = 1;
  }
  last_btnX = remoteData.btnX; 

  // 向右跳跃
  if (remoteData.btnB && !last_btnB  && robot_enabled) { 
    wrobot.jump = 1;
    right_jump_flag = 1; // 跳跃
  }
  last_btnB = remoteData.btnB; 

  // 向前跳跃
  if (remoteData.btnY && !last_btnY  && robot_enabled) { 
    wrobot.jump = 1;
    forward_jump_flag = 1;
  }
  last_btnY = remoteData.btnY; 

  // 向后跳跃
  if (remoteData.btnA && !last_btnA  && robot_enabled) { 
    wrobot.jump = 1;
    back_jump_flag = 1;
  }
  last_btnA = remoteData.btnA; 

  // trigRT 根据扳机键按压力度调整振动力度
  if (xboxController.xboxNotif.trigRT > 0) {
      XboxtrigRTVibration();
  }

  // 需要再按一次进入越障模式
  if(!obstacle_mode && last_trigRT == 0 && xboxController.xboxNotif.trigRT > 0 && robot_enabled){
    obstacle_mode = true; // 可以进入越障模式
  }
  // 进入越障模式
  trigRT = xboxController.xboxNotif.trigRT;
  if (trigRT > 0 && obstacle_mode && robot_enabled) {
    obstacle_btn_down++;
    wrobot.joyy = 20;
  }
    // 越障模式进入过程取消
  if (trigRT == 0 && last_trigRT > 0) {
    obstacle_cancel(); 
    setLEDColor(CRGB::Black);
  }
  last_trigRT = trigRT;

  // btnShare 晃头，点头
  if (remoteData.btnShare && !last_btnShare && robot_enabled && !isExecutingAction) {
    isExecutingAction = true;          // 标记开始执行动作
    currentAction = &action_say_hello; // 指定执行的动作
    stepStartTime = millis();          // 记录第一步开始时间
    currentStepIndex = 0;              // 从第一步开始
  }
  last_btnShare = remoteData.btnShare; // 更新btnY的上一次状态

  // 超级平衡模式切换
  if (remoteData.btnSelect && !last_btnSelect) { 

      pid_super_balance.error_prev = 0.0f;

      super_balance_mode = !super_balance_mode;
      if (super_balance_mode) {
        setLEDColor(CRGB::Blue);
        zaccel_threshold = super_zaccel_threshold;
        resetZeroPoint(); // 重置位移零点，确保初始平衡
      }else{
        cancel_super_mode();
      }
  }
  last_btnSelect = remoteData.btnSelect;


  // 舵机中位校准 中间十字左右键同时按下
  if (calibrationOfs_flag == 0 && remoteData.btnSelect && last_btnSelect && remoteData.btnStart && last_btnStart) { 
      calibrationOfs_flag =1;
      sms_sts.CalibrationOfs(ID[0]);
      sms_sts.CalibrationOfs(ID[1]);
      XboxKeyVibration(100,2000); // 震动
      Serial.println("CalibrationOfs");
  }
  if(calibrationOfs_flag > 0){
     calibrationOfs_flag ++;;
  }
  if(calibrationOfs_flag > 500){
    calibrationOfs_flag =0;
  }

  last_btnSelect = remoteData.btnSelect;
  last_btnStart = remoteData.btnStart;
}
#endif

// 执行连续动作的函数
void runActionSequence() {
  // 若未执行动作或动作指针为空，直接返回
  if (!isExecutingAction || currentAction == NULL) {
    return;
  }

  // 获取当前时间
  unsigned long currentTime = millis();
  
  // --------------- 停顿逻辑处理 ---------------
  if (isPausing) {
    // 停顿期间不更新任何控制参数（保持上一步状态）
    unsigned long pauseElapsed = currentTime - pauseStartTime;
    
    // 停顿时间结束，切换到下一步
    if (pauseElapsed >= currentPauseDuration) {
      isPausing = false;
      currentStepIndex++;
      stepStartTime = currentTime; // 记录新步骤开始时间
      // Serial.print("停顿结束，切换到步骤：");
      // Serial.println(currentStepIndex);
    }
    return; // 停顿期间直接返回，不执行后续逻辑
  }

  // --------------- 正常步骤执行逻辑 ---------------
  unsigned long elapsedTime = currentTime - stepStartTime;
  ActionStep currentStep = currentAction->steps[currentStepIndex];

  // 检查当前步骤是否为结束标记
  if (currentStep.stepDuration == 0) {
    isExecutingAction = false;
    currentStepIndex = 0;
    currentAction = NULL;
    // 重置参数
    wrobot.joyx = 0;
    wrobot.joyy = 0;
    wrobot.jump = 0;
    roll_adjust = 0.0f;
    leg_height_base = 20.0f;
    wrobot.acc0 = SERVO0_ACC;   // 恢复默认加速度
    wrobot.acc1 = SERVO1_ACC;
    wrobot.speed0 = SERVO0_SPEED; // 恢复默认速度
    wrobot.speed1 = SERVO1_SPEED;
    return;
  }

  // 更新当前步骤的控制参数
  wrobot.joyx = currentStep.joyx;
  wrobot.joyy = currentStep.joyy;
  wrobot.jump = currentStep.jump;
  roll_adjust = currentStep.roll_adjust;
  leg_height_base = currentStep.leg_height_base;

   // 舵机参数同步：动作集定义了则覆盖，未定义则保持wrobot的默认值（全局常量）
  wrobot.acc0 = (currentStep.acc0 != -1) ? currentStep.acc0 : SERVO0_ACC;
  wrobot.acc1 = (currentStep.acc1 != -1) ? currentStep.acc1 : SERVO1_ACC;
  wrobot.speed0 = (currentStep.speed0 != -1) ? currentStep.speed0 : SERVO0_SPEED;
  wrobot.speed1 = (currentStep.speed1 != -1) ? currentStep.speed1 : SERVO1_SPEED;

  if (elapsedTime >= currentStep.stepDuration) {
    // 若设置了停顿时间，进入停顿状态
    if (currentStep.pauseDuration > 0) {
      isPausing = true;
      currentPauseDuration = currentStep.pauseDuration; // 记录当前需要停顿的时间
      pauseStartTime = currentTime;
    } else {
      // 无停顿，直接切换到下一步
      currentStepIndex++;
      stepStartTime = currentTime;
    }
  }
}

// 退出超级平衡模式
void cancel_super_mode(){
  super_balance_mode = false;
  zaccel_threshold = normal_zaccel_threshold;
  setLEDColor(CRGB::Black);
  resetZeroPoint(); 
}

// 障碍检测
void obstacle_detect() {

  // 自动越障前进模式下,持续300次循环，防止刚启动由于惯性无法立即向前
  if(!obstacle_btn_down){
    return;
  }

  // 越障高度检测
  // if( trigRT > 0 && trigRT < 400 ){ // 蓝灯，最低障碍
  //   obstacle_level = "low";
  //   setLEDColor(CRGB::Blue);
  //   obstacle_pitch = low_obstacle_pitch;
  //   servo_up_height = low_servo_up_height;
  //   servo_down_height = low_servo_down_height;
  //   motor_target = low_motor_target;
    
  // }else 
  if( trigRT >= 900  ){ // 红灯，最高障碍
    obstacle_level = "high";
    setLEDColor(CRGB::Red);
    obstacle_pitch = high_obstacle_pitch;
    servo_up_height = high_servo_up_height;
    servo_down_height = high_servo_down_height;
    motor_target = high_motor_target;
    

  }else{ // 黄灯，中度障碍
    obstacle_level = "mid";
    setLEDColor(CRGB::Yellow);
    obstacle_pitch = mid_obstacle_pitch;
    servo_up_height = mid_servo_up_height;
    servo_down_height = mid_servo_down_height; 
    motor_target = mid_motor_target;
  }

  // 使其向前倾斜，防止向后倒
  pitch_adjust = -2.0f;

  // 检测速度停滞
  if ( obstacle_btn_down > 300 
    && obstacle_enconter_count == 0
    && LQR_speed > 0.0f && LQR_speed < 0.1f
      && robot_enabled && !sitting_down && !jump_flag 
  ){
      obstacle_enconter_count = 1;
  }
}

// 磕头越障
void obstacle_loop()
{
  if(obstacle_enconter_count == 0){
    return;
  }

  obstacle_enconter_count++;

  if(obstacle_pitch_adjust_count > 0){
    obstacle_pitch_adjust_count++;
    pitch_adjust -=5.0f;
  }

  if(obstacle_motor_go_count > 0){
    obstacle_motor_go_count++;
  }

  // 向前倾斜
  if (obstacle_status == "" && obstacle_enconter_count == 5){
    obstacle_status = "pitch_adjust";
    obstacle_pitch_adjust_count = 1;

    // 如果扳机键按到底，伸长腿
    if (obstacle_pitch_adjust_count == 1 &&  obstacle_level == "high" ){
      ACC[0] = ACC[1] = 150;
      Speed[0] = Speed[1] = 700;
      Position[0] = (SERVO0_MIN + servo_up_height); // MAX=390
      Position[1] = (SERVO1_MIN - servo_up_height); 
      sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
      startLEDBlink(CRGB::Yellow, 100,  1 );
    }
  }
   
  // motor_go:向前滚动 + 舵机收缩(shrink)
   if( obstacle_status == "pitch_adjust_over" && !sms_sts.ReadMove(ID[0]) && !sms_sts.ReadMove(ID[1]) ){
      obstacle_status = "motor_go";
      obstacle_pitch_adjust_count = 0;
      obstacle_motor_go_count = 1;
      pitch_adjust = 0.0f;

      // 收缩腿
      if(obstacle_motor_go_count == 1){
        ACC[0] = ACC[1] = 250;
        Speed[0] = Speed[1] = 2500;
        Position[0] = (SERVO0_MIN + servo_down_height);
        Position[1] = (SERVO1_MIN - servo_down_height); 
        sms_sts.SyncWritePosEx(ID, 2, Position, Speed, ACC);
      }
  }

  // 舵机收缩完毕
  if((
      obstacle_status == "motor_go" &&  obstacle_motor_go_count > 10 
      && !sms_sts.ReadMove(ID[0]) && !sms_sts.ReadMove(ID[1])
    ) || obstacle_status == "obstacle_failed"){ 
    
    obstacle_cancel();
    resetZeroPoint();   
   
    startLEDBlink(CRGB::Green, 100,  2 );
  }
}


void obstacle_cancel()
{
    obstacle_btn_down = 0;
    obstacle_status = "";
    
    obstacle_enconter_count = 0;

    obstacle_motor_go_count = 0;

    obstacle_pitch_adjust_count = 0;

    pitch_adjust = 0.0f; //负数前倾，正数后倾
    obstacle_mode = false; // 跳出越障模式
}
