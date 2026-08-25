/*
  Types.h — Core enums, structs, and type definitions for MoistureController
  Ultra.

  Pure data — no logic, no side effects. Used by state machine, safety, persist.
  Packed layouts are the EEPROM on-wire format (REASONING.md §6).

  License: MIT (see root LICENSE)
*/

#pragma once

#include <stdint.h>

#ifdef __GNUC__
#define MC_PACKED __attribute__((packed))
#else
#define MC_PACKED
#endif

// =============================================================================
// High-level operating states (persisted to EEPROM on change)
// =============================================================================
// REASONING.md §5 (Robustness Architecture — 6-Layer Defense-in-Depth)
enum class SystemState : uint8_t {
  NORMAL = 0,
  FAULT = 1,
  CALIBRATE = 2,
  MANUAL = 3
};

// =============================================================================
// Reasons for entering FAULT (and for refuse log codes)
// =============================================================================
// REASONING.md §5 (FAULT entry reasons), §4 (FMEA table)
enum class FaultReason : uint8_t {
  NONE = 0,
  SENSOR_DEAD = 1,
  NO_RESPONSE_TO_WATER = 2,
  DAILY_BUDGET_EXCEEDED = 3,
  LOW_BATTERY_PERSISTENT = 4,
  MANUAL_FAULT = 5,
  EEPROM_CORRUPT = 6,
  RTC_LOST = 7,
  OUTSIDE_WINDOW = 8,
  LOW_BATTERY = 9,
  SENSOR_IMPLAUSIBLE = 10,
  WET_ENOUGH = 11
};

enum class EventAction : uint8_t {
  REFUSE = 0,
  OPEN_PULSE = 1,
  CLOSE_PULSE = 2,
  FAULT = 3,
  WAKE = 4,
  CAL = 5,
  BOOT = 6,
  ABSORB_OK = 7
};

enum PersistFlags : uint8_t {
  FLAG_AWAITING_ABSORPTION = 1u << 0,
  FLAG_NEEDS_CAL = 1u << 1,
  FLAG_LOW_BATT = 1u << 2,
  FLAG_SENSOR_STEMMA = 1u << 3,
  FLAG_HAVE_LAST_MOISTURE = 1u << 4
};

enum WaterDecision : uint8_t { WD_NONE = 0, WD_WATER = 1, WD_WET = 2 };

struct CivilTime {
  uint16_t year;
  uint8_t month;
  uint8_t day;
  uint8_t hour;
  uint8_t minute;
  uint8_t second;
};

// REASONING.md §7 (Calibration and Threshold Philosophy)
struct CalibrationData {
  uint16_t dry_min;
  uint16_t wet_max;
  int8_t threshold_offset;
  uint8_t reserved;
  uint32_t last_cal_timestamp;
} MC_PACKED;

// REASONING.md §6 (Persistent State — EEPROM Layout)
struct EventLogEntry {
  uint32_t timestamp;
  uint8_t action;
  uint8_t reason;
  uint16_t moisture;
  uint16_t vbat_mv;
  uint8_t extra[6];
} MC_PACKED;

struct PersistentState {
  uint16_t magic;
  uint8_t version;
  uint8_t state;
  uint8_t fault_reason;
  uint8_t flags;
  uint8_t no_response_count;
  uint8_t sensor_fail_count;
  uint8_t water_pulses_today;
  uint8_t last_water_day;
  uint8_t last_water_month;
  uint8_t log_head;
  CalibrationData cal;
  uint16_t last_moisture;
  uint16_t last_vbat_mv;
  uint16_t pulse_ms;
  uint8_t absorption_min;
  uint8_t max_pulses_day;
  uint8_t w1_start;
  uint8_t w1_end;
  uint8_t w2_start;
  uint8_t w2_end;
  uint8_t low_th_pct;
  uint8_t high_th_pct;
  uint16_t min_response_delta;
  uint8_t low_batt_streak;
  uint8_t reserved2;
  uint16_t crc;
} MC_PACKED;

// magic..reserved2 inclusive, excluding crc (2)
#define PERSIST_CRC_BYTES (sizeof(PersistentState) - sizeof(uint16_t))

static_assert(sizeof(CalibrationData) == 10, "CalibrationData packing");
static_assert(sizeof(EventLogEntry) == 16, "EventLogEntry packing");
static_assert(sizeof(PersistentState) == 42, "PersistentState packing");
static_assert(sizeof(PersistentState) <= 64, "persist slot too small");
