/*
  Power.h — Power rail control + lowest-leakage pin configuration.

  Called on every boot and immediately before each deep sleep.
  No external libraries beyond core avr/ and Arduino pin APIs.

  License: MIT (see root LICENSE)
*/

#pragma once

#include <Arduino.h>
#include <avr/io.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <stdint.h>

#include "Config.h"

// =============================================================================
// power_init_lowest_leakage
// =============================================================================
// Configures GPIO, PRR, DIDR0, comparator, and WDT for minimum sleep current
// on a genuine ATmega328P in SLEEP_MODE_PWR_DOWN.
// Expected MCU contribution: 0.1–0.5 µA at 3.3 V (datasheet, genuine silicon).
// REASONING.md §2.4, §3, §2.3
inline void power_init_lowest_leakage() {
  PRR = 0xFF;
  wdt_disable();

  ACSR |= (1 << ACD);

  // DIDR0 has six ADC buffer-disable bits on ATmega328P (ADC0..ADC5).
  // REASONING.md §2.3 — 0x3F, not 0xFF.
  DIDR0 = 0x3F;

  // GPIO sweep. Exceptions MUST stay inputs:
  //   RTC INT, user button, VBAT divider, SDA/SCL (external pull-ups).
  // Driving SDA/SCL or VBAT as outputs fights pull-ups / the divider and
  // wastes tens to hundreds of microamps. REASONING.md §2.4, §3
  for (uint8_t p = 0; p < 20; p++) {
    // UART pins: leave as inputs. On a Nano/Pro Mini they are wired to the
    // USB-serial chip; driving them fights that transceiver.
    if (p == 0 || p == 1) {
      pinMode(p, INPUT);
      digitalWrite(p, LOW);
      continue;
    }
    if (p == RTC_INT_PIN) {
      pinMode(p, INPUT_PULLUP);
    } else if (p == USER_BUTTON_PIN) {
      pinMode(p, INPUT_PULLUP);
    } else if (p == (uint8_t)VBAT_SENSE_PIN || p == (uint8_t)I2C_SDA_PIN ||
               p == (uint8_t)I2C_SCL_PIN) {
      pinMode(p, INPUT);
      digitalWrite(p, LOW);
    } else if (p == SENSOR_POWER_GATE_PIN || p == VALVE_POWER_GATE_PIN) {
      pinMode(p, OUTPUT);
      digitalWrite(p, HIGH);
    } else if (p == VALVE_IN1_PIN || p == VALVE_IN2_PIN) {
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
    } else if (p == (uint8_t)MOISTURE_ANALOG_PIN) {
      // Sensor rail is gated off: hold the analog node low to kill leakage.
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
    } else {
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
    }
  }
}

inline void sensor_rail_enable(bool on) {
  pinMode(SENSOR_POWER_GATE_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_GATE_PIN, on ? LOW : HIGH);
}

inline void valve_rail_enable(bool on) {
  pinMode(VALVE_POWER_GATE_PIN, OUTPUT);
  digitalWrite(VALVE_POWER_GATE_PIN, on ? LOW : HIGH);
}

inline void adc_clock_on() {
  PRR &= ~(1 << PRADC);
  ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

inline void adc_clock_off() {
  ADCSRA = 0;
  PRR |= (1 << PRADC);
}

// Battery millivolts via the configured divider. Returns 0 on a dead ADC
// (fail-closed: Layer 1 will refuse watering). REASONING.md §5 Layer 1/4
inline uint16_t read_battery_mv() {
  adc_clock_on();
  analogReference(DEFAULT);
  pinMode(VBAT_SENSE_PIN, INPUT);
  (void)analogRead(VBAT_SENSE_PIN);
  uint32_t acc = 0;
  for (uint8_t i = 0; i < 8; i++) {
    acc += analogRead(VBAT_SENSE_PIN);
  }
  uint16_t adc = (uint16_t)(acc / 8);
  adc_clock_off();

  uint32_t mv = (uint32_t)adc * (uint32_t)AREF_MV;
  mv *= (VBAT_R_TOP_OHM + VBAT_R_BOTTOM_OHM);
  mv /= 1023UL;
  mv /= VBAT_R_BOTTOM_OHM;
  if (mv > 65535UL) {
    mv = 65535UL;
  }
  return (uint16_t)mv;
}

// Enters SLEEP_MODE_PWR_DOWN with BOD disabled. INT0/INT1 (RTC / button)
// are the only wake sources. WDT is off.
// sleep_bod_disable() is a timed MCUCR sequence and MUST be adjacent to
// sleep_cpu() with interrupts restored only as sei + sleep (no gap).
// REASONING.md §2.4
inline void power_enter_deep_sleep() {
  power_init_lowest_leakage();
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  cli();
  sleep_enable();
  sleep_bod_disable();
  sei();
  sleep_cpu();
  sleep_disable();
}
