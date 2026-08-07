#include <Arduino.h>

uint8_t speed = 150;
bool direction = false;

void setup() {
  pinMode(6, OUTPUT); // Set pin 6 as an output DIR(Binaly)
  pinMode(7, OUTPUT); // Set pin 7 as an output PWM(Integer)
}

void loop() {
  analogWrite(6, speed); // Motor ON
  digitalWrite(7, 0); // Change Direction
  delay(1000); // Delay for 1 second
  analogWrite(6, 0); //Motor OFF
  delay(1000);
  analogWrite(6, speed); // Motor ON
  digitalWrite(7, 1); // Change Direction
  delay(1000); // Delay for 1 second
  analogWrite(6, 0); //Motor OFF
  delay(1000);
}