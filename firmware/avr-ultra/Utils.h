/*
  Utils.h — Busy-wait delay and I2C bring-up/teardown with PRR gating.

  delay() / millis() require Timer0. After PRR = 0xFF that timer is off, so
  all short waits use CPU-cycle _delay_ms(1) loops instead.
  REASONING.md §2.4, §3

  License: MIT (see root LICENSE)
*/

#pragma once

#include <Arduino.h>
#include <Wire.h>
#include <avr/io.h>
#include <util/delay.h>

#include "Config.h"

inline void busy_delay_ms(uint16_t ms) {
  while (ms--) {
    _delay_ms(1);
  }
}

inline void twi_on() {
  PRR &= ~(1 << PRTWI);
  Wire.begin();
  Wire.setClock(100000);
#if defined(WIRE_HAS_TIMEOUT)
  Wire.setWireTimeout(25000, true);
#endif
}

inline void twi_off() {
  Wire.end();
  PRR |= (1 << PRTWI);
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
  digitalWrite(I2C_SDA_PIN, LOW);
  digitalWrite(I2C_SCL_PIN, LOW);
}

// Clock out a stuck slave so the next transaction can start.
// REASONING.md §4 F9 (I2C bus lock-up)
inline void i2c_bus_recover() {
  Wire.end();
  pinMode(I2C_SCL_PIN, OUTPUT);
  pinMode(I2C_SDA_PIN, INPUT);
  for (uint8_t i = 0; i < 9; i++) {
    digitalWrite(I2C_SCL_PIN, HIGH);
    busy_delay_ms(1);
    digitalWrite(I2C_SCL_PIN, LOW);
    busy_delay_ms(1);
  }
  pinMode(I2C_SDA_PIN, OUTPUT);
  digitalWrite(I2C_SDA_PIN, LOW);
  digitalWrite(I2C_SCL_PIN, HIGH);
  busy_delay_ms(1);
  digitalWrite(I2C_SDA_PIN, HIGH);
  busy_delay_ms(1);
  pinMode(I2C_SDA_PIN, INPUT);
  pinMode(I2C_SCL_PIN, INPUT);
}

inline bool i2c_write_reg(uint8_t addr, uint8_t reg, const uint8_t *data,
                          uint8_t n) {
  Wire.beginTransmission(addr);
  if (Wire.write(reg) != 1) {
    Wire.endTransmission();
    return false;
  }
  if (n && Wire.write(data, n) != n) {
    Wire.endTransmission();
    return false;
  }
  return Wire.endTransmission() == 0;
}

inline bool i2c_write_u8(uint8_t addr, uint8_t reg, uint8_t v) {
  return i2c_write_reg(addr, reg, &v, 1);
}

inline bool i2c_read_reg(uint8_t addr, uint8_t reg, uint8_t *data, uint8_t n) {
  Wire.beginTransmission(addr);
  if (Wire.write(reg) != 1) {
    Wire.endTransmission();
    return false;
  }
  // STOP then read: more reliable on DS3231 clones than a repeated start.
  if (Wire.endTransmission() != 0) {
    return false;
  }
  busy_delay_ms(1);
  uint8_t got = (uint8_t)Wire.requestFrom(addr, n);
  if (got != n) {
    return false;
  }
  for (uint8_t i = 0; i < n; i++) {
    data[i] = (uint8_t)Wire.read();
  }
  return true;
}

inline bool button_held(uint16_t hold_ms) {
  if (digitalRead(USER_BUTTON_PIN) != LOW) {
    return false;
  }
  uint16_t waited = 0;
  while (waited < hold_ms) {
    if (digitalRead(USER_BUTTON_PIN) != LOW) {
      return false;
    }
    busy_delay_ms(10);
    waited = (uint16_t)(waited + 10);
  }
  return true;
}
