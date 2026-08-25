/*
  Config.h — Compile-time configuration, pin map, and constants for
  MoistureController Ultra (Milestone 1).

  This file centralizes every hardware mapping and tunable so changes are
  auditable. All values carry explicit traceability to REASONING.md.

  Arduino IDE compatible: placed alongside avr-ultra.ino in the sketch folder.

  License: MIT (see root LICENSE)
*/

#pragma once

// =============================================================================
// Debug output control (power-critical)
// =============================================================================
// Serial is a major source of leakage on boards with CH340/FTDI or even the
// TX pin itself when the USB-serial converter is present. It must be OFF in
// production Ultra builds.
// REASONING.md §3 (System Power Budget)
#define ENABLE_DEBUG_SERIAL 0

#if ENABLE_DEBUG_SERIAL
#define DEBUG_PRINT(x) Serial.print(x)
#define DEBUG_PRINTLN(x) Serial.println(x)
#define DEBUG_BEGIN() Serial.begin(9600)
#else
#define DEBUG_PRINT(x)                                                         \
  do {                                                                         \
  } while (0)
#define DEBUG_PRINTLN(x)                                                       \
  do {                                                                         \
  } while (0)
#define DEBUG_BEGIN()                                                          \
  do {                                                                         \
  } while (0)
#endif

// If 1 and ENABLE_DEBUG_SERIAL, a stopped DS3231 oscillator is loaded from
// compile-time __DATE__/__TIME__. Off by default: a wrong clock can water
// outside the intended window. REASONING.md §5 Layer 1 (RTC time is valid)
#define SET_RTC_FROM_BUILD 0

// =============================================================================
// Valve H-bridge pin map (DRV8833 or equivalent, single-coil latching solenoid)
// =============================================================================
// Safe de-energized state = both legs LOW. Close/open are opposite polarity
// pulses performed by Valve.h. Pins chosen to avoid I2C (A4/A5).
// REASONING.md §5 Layer 0, §2.2, §8
#define VALVE_IN1_PIN 9
#define VALVE_IN2_PIN 10

// Close pulse polarity (reverse of open). Swap these two if the valve moves
// the wrong way on first bring-up. REASONING.md §8
#define VALVE_CLOSE_IN1 LOW
#define VALVE_CLOSE_IN2 HIGH
#define VALVE_OPEN_IN1 HIGH
#define VALVE_OPEN_IN2 LOW

// =============================================================================
// Power-gating pins (P-channel MOSFET high-side switches, active LOW gate)
// =============================================================================
// Both rails start DISABLED (gate HIGH → P-MOS off).
// REASONING.md §2.3, §10
#define SENSOR_POWER_GATE_PIN 8
#define VALVE_POWER_GATE_PIN 7

// =============================================================================
// Wake / RTC / sense / user pins
// =============================================================================
// DS3231 INT/SQW is open-drain. An external pull-up is typical on ZS-042
// modules; we also enable the internal pull-up so a missing resistor still
// wakes. REASONING.md §2.1, §5 Layer 1
#define RTC_INT_PIN 2     // INT0
#define USER_BUTTON_PIN 3 // INT1, active LOW (INPUT_PULLUP)
#define VBAT_SENSE_PIN A2
#define MOISTURE_ANALOG_PIN A0
#define I2C_SDA_PIN A4
#define I2C_SCL_PIN A5

#define DS3231_I2C_ADDR 0x68
#define STEMMA_I2C_ADDR 0x36

// =============================================================================
// ADC / divider (Layer 1 and Layer 4 battery checks)
// =============================================================================
// 3.3 V AVCC, 100 k / 100 k divider → half-scale. Recalibrate if the board
// uses a different divider. REASONING.md §5 Layer 1/4, §3
#define AREF_MV 3300
#define VBAT_R_TOP_OHM 100000UL
#define VBAT_R_BOTTOM_OHM 100000UL
#define LOW_BATT_THRESHOLD_MV 3000
#define LOW_BATT_FAULT_STREAK 3

// =============================================================================
// Sensor acquisition (Layer 2)
// =============================================================================
// REASONING.md §9, §5 Layer 2
#define SENSOR_SAMPLE_COUNT 7
#define SENSOR_STABILIZE_MS 10
#define SENSOR_SAMPLE_GAP_MS 10
#define SENSOR_PLAUSIBLE_MIN 10
#define SENSOR_PLAUSIBLE_MAX 1013
#define STEMMA_PLAUSIBLE_MIN 50
#define STEMMA_PLAUSIBLE_MAX 2500
#define STEMMA_DEFAULT_DRY 200
#define STEMMA_DEFAULT_WET 1500
#define SENSOR_MAX_JUMP 450
#define SENSOR_FAIL_STREAK 3
#define MOISTURE_HIGHER_IS_WETTER 1

// Conservative first-boot range used until a real calibration exists.
// Fail-closed: a too-narrow default would water constantly.
// REASONING.md §7
#define DEFAULT_DRY_MIN 200
#define DEFAULT_WET_MAX 800

// =============================================================================
// Default timing / policy (copied into EEPROM on first boot)
// =============================================================================
// REASONING.md §8 pulse parameters, §5 Layers 1/3/5
#define DEFAULT_VALVE_PULSE_MS 50
#define VALVE_PULSE_MAX_MS 150
#define VALVE_RAIL_SETTLE_MS 5
#define VALVE_DEAD_TIME_MS 15
#define DEFAULT_ABSORPTION_MINUTES 30
#define DEFAULT_MAX_PULSES_PER_DAY 4
#define DEFAULT_LOW_TH_PCT 35
#define DEFAULT_HIGH_TH_PCT 55
#define DEFAULT_MIN_RESPONSE_DELTA 25
#define NO_RESPONSE_FAULT_STREAK 3

// Irrigation windows [start, end) local hours. Alarms fire at each start.
// REASONING.md §5 Layer 1 (allowed irrigation window)
#define DEFAULT_W1_START_H 5
#define DEFAULT_W1_END_H 8
#define DEFAULT_W2_START_H 19
#define DEFAULT_W2_END_H 22

#define PERSIST_MAGIC 0xC0DE
#define PERSIST_VERSION 1
#define PERSIST_SLOT_BYTES 64
#define PERSIST_PRIMARY_ADDR 0
#define PERSIST_BACKUP_ADDR 64
#define EVENT_LOG_ADDR 128
#define EVENT_LOG_SLOTS 16
#define EVENT_LOG_SLOT_BYTES 16

#define BUTTON_HOLD_MS 2000
