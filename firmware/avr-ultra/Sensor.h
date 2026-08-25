/*
  Sensor.h — Power-gated analog capacitive probe, optional STEMMA at 0x36.

  Rail is on only for the few milliseconds needed to take samples.
  REASONING.md §2.3, §9, §5 Layer 2

  License: MIT (see root LICENSE)
*/

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Logic.h"
#include "Power.h"
#include "Utils.h"

struct SensorReading {
  uint16_t raw;
  bool valid;
  bool stemma;
};

inline bool stemma_hw_present() {
  uint8_t id = 0;
  // Seesaw STATUS_BASE=0x00, STATUS_HW_ID=0x01. 0x55 (SAMD09) or 0x87 (tiny).
  Wire.beginTransmission(STEMMA_I2C_ADDR);
  Wire.write((uint8_t)0x00);
  Wire.write((uint8_t)0x01);
  if (Wire.endTransmission() != 0) {
    return false;
  }
  busy_delay_ms(5);
  if (Wire.requestFrom((uint8_t)STEMMA_I2C_ADDR, (uint8_t)1) != 1) {
    return false;
  }
  id = (uint8_t)Wire.read();
  return id == 0x55 || id == 0x87;
}

inline bool stemma_read_raw(uint16_t *out) {
  Wire.beginTransmission(STEMMA_I2C_ADDR);
  Wire.write((uint8_t)0x0F); // TOUCH_BASE
  Wire.write((uint8_t)0x10); // channel 0
  if (Wire.endTransmission() != 0) {
    return false;
  }
  busy_delay_ms(5);
  if (Wire.requestFrom((uint8_t)STEMMA_I2C_ADDR, (uint8_t)2) != 2) {
    return false;
  }
  uint8_t hi = (uint8_t)Wire.read();
  uint8_t lo = (uint8_t)Wire.read();
  *out = (uint16_t)((hi << 8) | lo);
  return true;
}

inline uint16_t analog_read_averaged() {
  pinMode(MOISTURE_ANALOG_PIN, INPUT);
  adc_clock_on();
  analogReference(DEFAULT);
  (void)analogRead(MOISTURE_ANALOG_PIN);
  uint16_t samples[SENSOR_SAMPLE_COUNT];
  for (uint8_t i = 0; i < SENSOR_SAMPLE_COUNT; i++) {
    samples[i] = (uint16_t)analogRead(MOISTURE_ANALOG_PIN);
    busy_delay_ms(SENSOR_SAMPLE_GAP_MS);
  }
  adc_clock_off();
  pinMode(MOISTURE_ANALOG_PIN, OUTPUT);
  digitalWrite(MOISTURE_ANALOG_PIN, LOW);
  return average_trim_minmax(samples, SENSOR_SAMPLE_COUNT);
}

inline SensorReading sensor_read(bool prefer_stemma) {
  SensorReading r;
  r.raw = 0;
  r.valid = false;
  r.stemma = false;

  sensor_rail_enable(true);
  busy_delay_ms(SENSOR_STABILIZE_MS);

  if (prefer_stemma) {
    twi_on();
    uint16_t raw = 0;
    if (stemma_read_raw(&raw)) {
      r.raw = raw;
      r.stemma = true;
      r.valid = raw_in_plausible_range(raw, STEMMA_PLAUSIBLE_MIN,
                                       STEMMA_PLAUSIBLE_MAX);
      twi_off();
      sensor_rail_enable(false);
      return r;
    }
    twi_off();
  }

  r.raw = analog_read_averaged();
  r.valid = raw_in_plausible_range(r.raw, SENSOR_PLAUSIBLE_MIN,
                                   SENSOR_PLAUSIBLE_MAX);
  sensor_rail_enable(false);
  return r;
}

inline bool sensor_probe_stemma() {
  sensor_rail_enable(true);
  busy_delay_ms(SENSOR_STABILIZE_MS);
  twi_on();
  bool ok = stemma_hw_present();
  twi_off();
  sensor_rail_enable(false);
  return ok;
}
