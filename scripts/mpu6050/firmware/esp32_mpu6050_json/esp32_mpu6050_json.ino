/*
 * MPU-6050 → ESP32 → JSON serial example
 * --------------------------------------
 * 与网页 "MPU-6050 姿态遥测台" 配套的最小示例。
 *
 * 输出（每行一条 JSON，单位：deg / g / °/s / °C）：
 * {"roll":1.23,"pitch":-4.56,"yaw":78.90,"ax":0.01,"ay":0.02,"az":1.00,"gx":0.3,"gy":-0.1,"gz":1.2,"temp":31.5}
 *
 * 坐标与符号约定（与网页 3D 默认方向一致）：
 *  - 机体坐标：X+ 朝前，Y+ 朝左，Z+ 朝上（右手系）
 *  - Roll 正值 = 右侧下沉；Pitch 正值 = 抬头；Yaw 正值 = 俯视逆时针
 *  - 若 MPU-6050 在板上的安装方向不同，请先做轴交换/取反，再按本约定输出。
 *
 * 注意：
 *  - 网页默认波特率是 115200，必须与本 sketch 的 SERIAL_BAUD 一致；
 *    如果后续需要更高采样率，可同时把网页和固件改为 921600 等更高波特率。
 *  - 网页打开串口时，Arduino 串口监视器必须先关闭，串口不能被两个程序同时占用。
 *  - 没有磁力计时，Yaw 会缓慢漂移，这是 MPU-6050 的正常现象。
 *  - 若网页中的三维倾斜方向与实物相反，打开网页右上角的“视图校准”逐项反转即可。
 */

#include <Wire.h>
#include <math.h>

// ---------- 可修改的配置 ----------
constexpr uint32_t SERIAL_BAUD = 115200;
constexpr uint8_t MPU_ADDR = 0x68;       // AD0 接高电平时为 0x69
constexpr int PIN_SDA = 21;
constexpr int PIN_SCL = 22;
constexpr uint32_t SAMPLE_INTERVAL_US = 20000;   // 50 Hz
constexpr float RAD_TO_DEG = 57.295779513f;
constexpr float COMP_ALPHA = 0.98f;      // 互补滤波系数

// ---------- 运行时状态 ----------
float gyroBiasX = 0.0f;
float gyroBiasY = 0.0f;
float gyroBiasZ = 0.0f;
float rollDeg = 0.0f;
float pitchDeg = 0.0f;
float yawDeg = 0.0f;
uint32_t lastSampleUs = 0;
uint32_t readErrors = 0;

bool writeRegister(uint8_t reg, uint8_t value) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}

bool readRegisters(uint8_t reg, uint8_t *buffer, size_t length) {
  Wire.beginTransmission(MPU_ADDR);
  Wire.write(reg);
  if (Wire.endTransmission(false) != 0) {
    return false;
  }
  size_t received = Wire.requestFrom((uint8_t)MPU_ADDR, (uint8_t)length);
  if (received != length) {
    while (Wire.available()) Wire.read();
    return false;
  }
  for (size_t i = 0; i < length; ++i) {
    buffer[i] = Wire.read();
  }
  return true;
}

bool readImu(float &ax, float &ay, float &az,
             float &gx, float &gy, float &gz,
             float &tempC) {
  uint8_t raw[14];
  if (!readRegisters(0x3B, raw, sizeof(raw))) {
    return false;
  }

  int16_t axRaw = (int16_t)((raw[0] << 8) | raw[1]);
  int16_t ayRaw = (int16_t)((raw[2] << 8) | raw[3]);
  int16_t azRaw = (int16_t)((raw[4] << 8) | raw[5]);
  int16_t tRaw  = (int16_t)((raw[6] << 8) | raw[7]);
  int16_t gxRaw = (int16_t)((raw[8] << 8) | raw[9]);
  int16_t gyRaw = (int16_t)((raw[10] << 8) | raw[11]);
  int16_t gzRaw = (int16_t)((raw[12] << 8) | raw[13]);

  // 默认量程：±2 g（16384 LSB/g）与 ±250 °/s（131 LSB/(°/s)）。
  ax = axRaw / 16384.0f;
  ay = ayRaw / 16384.0f;
  az = azRaw / 16384.0f;
  gx = gxRaw / 131.0f - gyroBiasX;
  gy = gyRaw / 131.0f - gyroBiasY;
  gz = gzRaw / 131.0f - gyroBiasZ;
  tempC = tRaw / 340.0f + 36.53f;
  return true;
}

void calibrateGyroscope() {
  Serial.println("Calibrating gyro, keep the board still...");
  delay(600);

  constexpr int samples = 1000;
  double sx = 0.0, sy = 0.0, sz = 0.0;
  int valid = 0;

  for (int i = 0; i < samples; ++i) {
    uint8_t raw[14];
    if (readRegisters(0x3B, raw, sizeof(raw))) {
      int16_t gxRaw = (int16_t)((raw[8] << 8) | raw[9]);
      int16_t gyRaw = (int16_t)((raw[10] << 8) | raw[11]);
      int16_t gzRaw = (int16_t)((raw[12] << 8) | raw[13]);
      sx += gxRaw / 131.0;
      sy += gyRaw / 131.0;
      sz += gzRaw / 131.0;
      ++valid;
    }
    delay(2);
  }

  if (valid > 0) {
    gyroBiasX = (float)(sx / valid);
    gyroBiasY = (float)(sy / valid);
    gyroBiasZ = (float)(sz / valid);
  }
  Serial.printf("Gyro bias: %.3f, %.3f, %.3f deg/s\n",
                gyroBiasX, gyroBiasY, gyroBiasZ);
}

void setup() {
  Serial.begin(SERIAL_BAUD);
  delay(300);

  Wire.begin(PIN_SDA, PIN_SCL, 400000);
  Wire.setClock(400000);

  uint8_t whoAmI = 0;
  if (!readRegisters(0x75, &whoAmI, 1)) {
    Serial.println("ERROR: MPU-6050 not responding. Check SDA/SCL and power.");
    while (true) {
      delay(1000);
    }
  }
  Serial.printf("MPU-6050 WHO_AM_I = 0x%02X\n", whoAmI);

  writeRegister(0x6B, 0x80);   // reset
  delay(120);
  writeRegister(0x6B, 0x01);   // clock source = gyro X PLL
  writeRegister(0x1A, 0x03);   // digital low-pass filter, ~44 Hz
  writeRegister(0x19, 0x04);   // sample rate = 1 kHz / (1 + 4) = 200 Hz
  writeRegister(0x1B, 0x00);   // gyro range ±250 °/s
  writeRegister(0x1C, 0x00);   // accel range ±2 g

  calibrateGyroscope();

  // 用一次静止姿态初始化滤波器，避免开头慢慢收敛。
  float ax, ay, az, gx, gy, gz, tempC;
  if (readImu(ax, ay, az, gx, gy, gz, tempC)) {
    rollDeg = atan2f(ay, az) * RAD_TO_DEG;
    pitchDeg = atan2f(ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;
  }
  lastSampleUs = micros();
  Serial.println("READY");
}

void loop() {
  uint32_t nowUs = micros();
  if ((uint32_t)(nowUs - lastSampleUs) < SAMPLE_INTERVAL_US) {
    return;
  }

  float dt = (nowUs - lastSampleUs) / 1000000.0f;
  lastSampleUs += SAMPLE_INTERVAL_US;
  if (dt <= 0.0f || dt > 0.25f) {
    dt = SAMPLE_INTERVAL_US / 1000000.0f;
    lastSampleUs = nowUs;
  }

  float ax, ay, az, gx, gy, gz, tempC;
  if (!readImu(ax, ay, az, gx, gy, gz, tempC)) {
    ++readErrors;
    if ((readErrors % 50) == 1) {
      Serial.printf("WARN: read error (%lu)\n", (unsigned long)readErrors);
    }
    return;
  }

  // 本约定下：+gx 使 Roll 增大（右侧下沉），-gy 使 Pitch 增大（抬头），
  // +gz 使 Yaw 增大（俯视逆时针）。
  float accRoll = atan2f(ay, az) * RAD_TO_DEG;
  float accPitch = atan2f(ax, sqrtf(ay * ay + az * az)) * RAD_TO_DEG;

  rollDeg = COMP_ALPHA * (rollDeg + gx * dt) + (1.0f - COMP_ALPHA) * accRoll;
  pitchDeg = COMP_ALPHA * (pitchDeg - gy * dt) + (1.0f - COMP_ALPHA) * accPitch;
  yawDeg += gz * dt;

  // 把角度限制在易读区间；Yaw 用 ±180° 表示。
  if (rollDeg > 180.0f) rollDeg -= 360.0f;
  if (rollDeg < -180.0f) rollDeg += 360.0f;
  if (pitchDeg > 90.0f) pitchDeg = 90.0f;
  if (pitchDeg < -90.0f) pitchDeg = -90.0f;
  if (yawDeg > 180.0f) yawDeg -= 360.0f;
  if (yawDeg < -180.0f) yawDeg += 360.0f;

  char json[256];
  snprintf(json, sizeof(json),
           "{\"roll\":%.2f,\"pitch\":%.2f,\"yaw\":%.2f,"
           "\"ax\":%.3f,\"ay\":%.3f,\"az\":%.3f,"
           "\"gx\":%.1f,\"gy\":%.1f,\"gz\":%.1f,\"temp\":%.1f}",
           rollDeg, pitchDeg, yawDeg,
           ax, ay, az, gx, gy, gz, tempC);
  Serial.println(json);
}
