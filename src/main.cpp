#include <Arduino.h>
// 左：赤印に赤の線
// 右：赤印に黒/緑の線
// わに：上昇
// ケーブル：下降
const uint8_t PIN_DIR = 7;
const uint8_t PIN_PWM = 6;
const uint8_t PIN_SW_UP = 2;
const uint8_t PIN_SW_DOWN = 3;
const uint8_t MOTOR_SPEED = 100; // 速度（0-255）

void setup() {
  pinMode(PIN_DIR, OUTPUT);
  pinMode(PIN_PWM, OUTPUT);

  // スイッチピンをプルアップ設定
  pinMode(PIN_SW_UP, INPUT_PULLUP);
  pinMode(PIN_SW_DOWN, INPUT_PULLUP);
}

void loop() {
  // スイッチの状態を読み取る（押すとLOW、離すとHIGH）
  bool upPressed = (digitalRead(PIN_SW_UP) == LOW);
  bool downPressed = (digitalRead(PIN_SW_DOWN) == LOW);

  if (upPressed && !downPressed) {
    // UPのみ押されている場合
    digitalWrite(PIN_DIR, HIGH);
    analogWrite(PIN_PWM, MOTOR_SPEED);
  } 
  else if (downPressed && !upPressed) {
    // DOWNのみ押されている場合
    digitalWrite(PIN_DIR, LOW);
    analogWrite(PIN_PWM, MOTOR_SPEED);
  } 
  else {
    // 両方押されている、または両方離されている場合は停止
    analogWrite(PIN_PWM, 0);
  }
}