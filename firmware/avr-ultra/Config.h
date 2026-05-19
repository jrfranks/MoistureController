/*
  Config.h — Compile-time configuration, pin map, and constants for
  MoistureController Ultra.

  This file centralizes every hardware mapping and tunable so changes are
  auditable. All values carry explicit traceability to the governing constraints
  in REASONING.md.

  Arduino IDE compatible: placed alongside MoistureController.ino in the sketch
  folder. No .cpp files in Milestone 0 skeleton (see plan).

  License: MIT (see root LICENSE)
*/

#pragma once

// =============================================================================
// Debug output control (power-critical)
// =============================================================================
// Serial is a major source of leakage on boards with CH340/FTDI or even the
// TX pin itself when the USB-serial converter is present. It must be OFF in
// production Ultra builds.
// REASONING.md §3 (System Power Budget) — any always-on peripheral destroys the
// <5–10 µA target.
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

// =============================================================================
// Valve H-bridge pin map (DRV8833 or equivalent, single-coil latching solenoid)
// =============================================================================
// Two pins control polarity through the H-bridge.
// Safe "close" state on boot (Layer 0) = both legs LOW → zero coil current.
// Real close/open actions later performed by short timed polarity pulses in
// Valve.h. Pins chosen to avoid I2C (A4/A5) and leave common PWM/INT pins
// available. REASONING.md §5 (Layer 0 — Hardware & Boot Guarantees)
// REASONING.md §2.2 (Valve Technology — Latching + H-Bridge)
// REASONING.md §8 (Actuation — Latching Valve Driver)
#define VALVE_IN1_PIN 9  // H-bridge input 1 (polarity leg A)
#define VALVE_IN2_PIN 10 // H-bridge input 2 (polarity leg B)

// =============================================================================
// Power-gating pins (P-channel MOSFET high-side switches)
// =============================================================================
// Both rails start DISABLED (gate HIGH for P-MOS → switch open).
// Only enabled for milliseconds when actually needed.
// REASONING.md §2.3 (Sensor Power Strategy — Mandatory Power Gating)
// REASONING.md §10 (Power Architecture and Energy Storage)
#define SENSOR_POWER_GATE_PIN                                                  \
  8 // LOW = sensor rail powered; HIGH = rail off (0 µA)
#define VALVE_POWER_GATE_PIN 7 // LOW = H-bridge VM powered; HIGH = rail off

// =============================================================================
// Wake / RTC / sense pins
// =============================================================================
// DS3231 INT/SQW (open-drain) wired here. Will be INPUT, no pull-up.
// Battery voltage divider on A2 (future Layer 1/4 checks).
// REASONING.md §2.1 (Wake Source — DS3231 VBAT Alarm)
// REASONING.md §5 (Layer 1 — Pre-Wake / Pre-Action Checks)
#define RTC_INT_PIN 2     // INT0 — alarm wake source from DS3231
#define VBAT_SENSE_PIN A2 // ADC input for battery voltage (divider scaled)

// =============================================================================
// Default timing constants (will move to EEPROM config block later)
// =============================================================================
// These are safe starting values; real values persisted and user-tunable.
// REASONING.md §8 (Pulse parameters)
#define DEFAULT_VALVE_PULSE_MS 50 // 40–80 ms typical for latching solenoids
#define DEFAULT_ABSORPTION_MINUTES                                             \
  30 // Wait time after watering before re-sample

// =============================================================================
// Safety thresholds (Layer 1/4)
// =============================================================================
// Battery example: 3.0 V low-batt on LiFePO4 / 3.3 V rail system.
// Exact mV and divider ratio decided by hardware; placeholder here.
// REASONING.md §5 (Layer 1), §3 (power budget)
#define LOW_BATT_THRESHOLD_MV 3000

// =============================================================================
// End of Config.h — all pin and power decisions justified above
// =============================================================================
