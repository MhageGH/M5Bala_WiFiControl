#include "M5Bala2.h"
#define MPU9250_ID 0x71
#define MPU6050_ID 0x68

M5Bala2::M5Bala2() { wire = &Wire; }
M5Bala2::M5Bala2(TwoWire &w) { wire = &w; }

void M5Bala2::begin() {
  imu = new MPU6050(*wire);
  imu->begin();
  imu_id = i2c_readByte(MPU6050_ADDR, MPU6050_WHO_AM_I);
  setMotor(0, 0);
  K1 = 30;
  K2 = 32;
  K3 = 6.5;
  K4 = 5.2;
  K5 = 8;
  loop_interval = 0;
  left_offset = 0;
  right_offset = 0;
  forward_offset = 0;
  for (int i = 0; i < 128; i++) imu->update();
  pitch = imu->getAngleX();
}

uint8_t M5Bala2::i2c_readByte(uint8_t address, uint8_t subAddress) {
  uint8_t data;                      // `data` will store the register data
  wire->beginTransmission(address);  // Initialize the Tx buffer
  wire->write(subAddress);           // Put slave register address in Tx buffer
  wire->endTransmission(false);  // Send the Tx buffer, but send a restart to
                                 // keep connection alive
  wire->requestFrom(address,
                    (uint8_t)1);  // Read one byte from slave register address
  data = Wire.read();             // Fill Rx buffer with result
  return data;                    // Return data read from slave register
}

void M5Bala2::setMotor(int16_t pwm0, int16_t pwm1) {
  // Value range
  int16_t m0 = constrain(pwm0, -255, 255);
  int16_t m1 = constrain(pwm1, -255, 255);

  // Dead zone
  if (((m0 > 0) && (m0 < DEAD_ZONE)) || ((m0 < 0) && (m0 > -DEAD_ZONE))) m0 = 0;
  if (((m1 > 0) && (m1 < DEAD_ZONE)) || ((m1 < 0) && (m1 > -DEAD_ZONE))) m1 = 0;

  // Same value
  static int16_t pre_m0, pre_m1;
  if ((m0 == pre_m0) && (m1 == pre_m1)) return;
  pre_m0 = m0;
  pre_m1 = m1;

  // Send I2C
  wire->beginTransmission(M5GO_WHEEL_ADDR);
  wire->write(MOTOR_CTRL_ADDR);  // Motor ctrl reg addr
  wire->write(((uint8_t *)&m0)[0]);
  wire->write(((uint8_t *)&m0)[1]);
  wire->write(((uint8_t *)&m1)[0]);
  wire->write(((uint8_t *)&m1)[1]);
  wire->endTransmission();
}

void M5Bala2::readEncder() {
  static float _speed_input0 = 0, _speed_input1 = 0;
  int16_t rx_buf[2];

  // Get Data from Module.
  wire->beginTransmission(M5GO_WHEEL_ADDR);
  wire->write(ENCODER_ADDR);  // encoder reg addr
  wire->endTransmission();
  wire->beginTransmission(M5GO_WHEEL_ADDR);
  wire->requestFrom(M5GO_WHEEL_ADDR, 4);

  if (wire->available()) {
    ((uint8_t *)rx_buf)[0] = wire->read();
    ((uint8_t *)rx_buf)[1] = wire->read();
    ((uint8_t *)rx_buf)[2] = wire->read();
    ((uint8_t *)rx_buf)[3] = wire->read();

    // filter
    _speed_input0 *= 0.9;
    _speed_input0 += 0.1 * rx_buf[0];
    _speed_input1 *= 0.9;
    _speed_input1 += 0.1 * rx_buf[1];

    speed_input0 = constrain((int16_t)(-_speed_input0), -255, 255);
    speed_input1 = constrain((int16_t)(_speed_input1), -255, 255);
  }
}

void M5Bala2::PIDCompute() {
  static float last_angle;
  static int16_t last_wheel;
  float angle, angle_velocity;
  int16_t wheel, wheel_velocity;
  int16_t torque, speed_diff, speed_diff_adjust;

  angle = pitch - angle_offset;
  angle_velocity = angle - last_angle;
  last_angle = angle;

  wheel =
      (speed_input0 + speed_input1) / 2; /* wheel = read_encoder()-profiler() */
  wheel_velocity = wheel - last_wheel;
  last_wheel = wheel;

  torque = (angle_velocity * K1) + (angle * K2) + (wheel_velocity * K3) +
           (wheel * K4);

  speed_diff = (speed_input0 - speed_input1);
  speed_diff_adjust = (K5 * speed_diff);

  pwm_out0 = torque - speed_diff_adjust;
  pwm_out1 = torque;
  pwm_out0 = constrain(pwm_out0, -255, 255);
  pwm_out1 = constrain(pwm_out1, -255, 255);
}

void M5Bala2::run() {
  if (micros() >= loop_interval) {
    loop_interval = micros() + 10000;
    imu->update();
    pitch = imu->getAngleX() + angle_offset;
    if (imu_id == MPU9250_ID) pitch = -pitch;
    if (abs(pitch) > 45) {
      setMotor(0, 0);
      return;
    }
    readEncder();
    PIDCompute();
    setMotor(
        pwm_out0 + forward_offset +
            left_offset,  // 5°ぐらいの傾きでpwm_outは引数の最大最小値±255に達する
        pwm_out1 + forward_offset + right_offset);
  }
}

void M5Bala2::stop() {
  left_offset = 0;
  right_offset = 0;
  forward_offset = 0;
}

void M5Bala2::move(int16_t speed) { forward_offset = -speed; }

void M5Bala2::turn(int16_t speed) {
  if (speed > 0) {
    left_offset = speed;
    right_offset = 0;

  } else if (speed < 0) {
    left_offset = 0;
    right_offset = -speed;
  }
}

void M5Bala2::rotate(int16_t speed) {
  left_offset = speed;
  right_offset = -speed;
}
