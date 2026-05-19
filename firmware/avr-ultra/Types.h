/*
  Types.h — Core enums, structs, and type definitions for MoistureController
  Ultra.

  Pure data — no logic, no side effects. Used by state machine, safety, persist,
  etc. Placed in the sketch folder for Arduino IDE single-folder compilation.

  All type choices (uint8_t packing, explicit enums) are deliberate for EEPROM
  storage size and flash code size on ATmega328P.

  License: MIT (see root LICENSE)
*/

#pragma once

#include <stdint.h>

// =============================================================================
// High-level operating states (persisted to EEPROM on change)
// =============================================================================
// REASONING.md §5 (Robustness Architecture — 6-Layer Defense-in-Depth)
// State machine transitions are only allowed through the Safety layer.
enum class SystemState : uint8_t {
  NORMAL = 0, // Normal scheduled operation
  FAULT = 1,  // Permanent safe mode — valve forced closed, human intervention
              // required
  CALIBRATE = 2, // One-time or on-demand self-calibration mode
  MANUAL = 3     // Temporary user override (time-bounded for safety)
};

// =============================================================================
// Reasons for entering FAULT state (stored with timestamp in log)
// =============================================================================
// These map 1:1 to the FMEA entries that can produce unrecoverable safe state.
// REASONING.md §5 (FAULT entry reasons)
// REASONING.md §4 (FMEA table)
enum class FaultReason : uint8_t {
  NONE = 0,        // No fault (used for NORMAL)
  SENSOR_DEAD = 1, // N consecutive implausible readings (Layer 2)
  NO_RESPONSE_TO_WATER =
      2, // Moisture did not rise after M watering attempts (Layer 5)
  DAILY_BUDGET_EXCEEDED = 3, // Configured daily water limit reached (Layer 1)
  LOW_BATTERY_PERSISTENT =
      4,            // Battery below threshold on multiple checks (Layer 1/4)
  MANUAL_FAULT = 5, // User-requested fault via button sequence
  EEPROM_CORRUPT =
      6,       // CRC failure on critical block with no valid backup (Layer 6)
  RTC_LOST = 7 // RTC time invalid and unrecoverable (Layer 1)
};

// =============================================================================
// Minimal calibration block (will be extended in Persist.h)
// =============================================================================
// Stored in EEPROM. First-boot self-cal populates dry/wet.
// REASONING.md §7 (Calibration and Threshold Philosophy)
struct CalibrationData {
  uint16_t dry_min;        // Raw ADC when probe is in air / known dry soil
  uint16_t wet_max;        // Raw ADC after thorough watering + absorption
  int8_t threshold_offset; // % offset from midpoint (positive = water later)
  uint32_t last_cal_timestamp; // RTC epoch seconds at last calibration
};

// =============================================================================
// Single event log entry (circular 16-deep black box)
// =============================================================================
// Written on every decision/action. Survives power loss.
// REASONING.md §6 (Persistent State — EEPROM Layout)
struct EventLogEntry {
  uint32_t timestamp; // RTC seconds since epoch (or 0 if unknown)
  uint8_t action;     // e.g. 0=refuse, 1=open pulse, 2=close pulse, 3=fault
  uint16_t moisture;  // validated reading at decision time
  uint16_t vbat_mv;   // battery voltage at decision time
  uint8_t reason;     // FaultReason or other code
  uint8_t extra[5];   // reserved for future (pulse length used, temp, etc.)
};

// =============================================================================
// End of Types.h
// =============================================================================
