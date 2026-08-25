/*
  Valve.h — Latching solenoid via H-bridge polarity pulses.

  Zero holding current. Rails are gated except for the pulse window.
  REASONING.md §2.2, §8, §5 Layer 0 / Layer 4

  License: MIT (see root LICENSE)
*/

#pragma once

#include <Arduino.h>

#include "Config.h"
#include "Logic.h"
#include "Power.h"
#include "Utils.h"

inline void valve_pins_safe() {
  pinMode(VALVE_IN1_PIN, OUTPUT);
  pinMode(VALVE_IN2_PIN, OUTPUT);
  digitalWrite(VALVE_IN1_PIN, LOW);
  digitalWrite(VALVE_IN2_PIN, LOW);
  pinMode(VALVE_POWER_GATE_PIN, OUTPUT);
  digitalWrite(VALVE_POWER_GATE_PIN, HIGH);
}

inline void valve_pulse(uint8_t in1, uint8_t in2, uint16_t ms) {
  uint8_t pulse = cap_pulse_ms(ms, VALVE_PULSE_MAX_MS);
  valve_rail_enable(true);
  busy_delay_ms(VALVE_RAIL_SETTLE_MS);
  digitalWrite(VALVE_IN1_PIN, in1);
  digitalWrite(VALVE_IN2_PIN, in2);
  busy_delay_ms(pulse);
  digitalWrite(VALVE_IN1_PIN, LOW);
  digitalWrite(VALVE_IN2_PIN, LOW);
  busy_delay_ms(VALVE_DEAD_TIME_MS);
  valve_rail_enable(false);
}

// Layer 0: actual close pulse, not merely de-energize. A latching valve left
// open stays open if we only drive both legs LOW. REASONING.md §5 Layer 0, F3
inline void valve_force_close(uint16_t pulse_ms) {
  valve_pins_safe();
  valve_pulse(VALVE_CLOSE_IN1, VALVE_CLOSE_IN2, pulse_ms);
}

inline void valve_open(uint16_t pulse_ms) {
  valve_pulse(VALVE_OPEN_IN1, VALVE_OPEN_IN2, pulse_ms);
}
