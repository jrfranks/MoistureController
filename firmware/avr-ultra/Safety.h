/*
  Safety.h — I/O adapter around Policy.h evaluate_cycle.

  REASONING.md §5

  License: MIT (see root LICENSE)
*/

#pragma once

#include <string.h>

#include "Persist.h"
#include "Policy.h"
#include "Power.h"
#include "RTC_DS3231.h"
#include "Sensor.h"
#include "Valve.h"

struct SafetyResult {
  bool watered;
  bool faulted;
  FaultReason reason;
};

inline void safety_enter_fault(FaultReason r, uint16_t moisture, uint16_t vbat,
                               uint32_t ts) {
  g_state.state = (uint8_t)SystemState::FAULT;
  g_state.fault_reason = (uint8_t)r;
  persist_log(EventAction::FAULT, r, moisture, vbat, ts);
  persist_save();
  valve_force_close(g_state.pulse_ms);
  DEBUG_PRINT(F("FAULT "));
  DEBUG_PRINTLN((uint8_t)r);
}

inline bool safety_schedule_next(const CivilTime *now, bool absorbing) {
  uint8_t hh = 0;
  uint8_t mm = 0;
  uint8_t add_days = 0;
  if (absorbing) {
    uint16_t add = g_state.absorption_min;
    if (add == 0) {
      add = 1;
    }
    add_minutes(now->hour, now->minute, add, &hh, &mm, &add_days);
    (void)add_days;
    return rtc_set_alarm1(hh, mm, 0);
  }
  next_daily_alarm(now->hour, g_state.w1_start, g_state.w2_start, &hh,
                   &add_days);
  (void)add_days;
  return rtc_set_alarm1(hh, 0, 0);
}

inline SafetyResult safety_apply(const CycleOutcome &o, const CivilTime *now) {
  SafetyResult r;
  r.watered = o.cmd == CMD_OPEN;
  r.faulted = (o.cmd == CMD_FAULT || o.cmd == CMD_HOLD_FAULT);
  r.reason = o.reason;
  if (o.log) {
    persist_log(o.log_action, o.reason, o.moisture, o.vbat, o.timestamp);
  }
  if (o.persist) {
    persist_save();
  }
  if (o.cmd == CMD_OPEN) {
    valve_open(g_state.pulse_ms);
  } else if (o.cmd == CMD_FAULT || o.cmd == CMD_HOLD_FAULT) {
    valve_force_close(g_state.pulse_ms);
  }
  if (o.schedule) {
    (void)safety_schedule_next(now, o.schedule_soak);
  }
  return r;
}

inline SafetyResult safety_run_cycle(bool rtc_wake) {
  CycleView in;
  memset(&in, 0, sizeof(in));
  in.rtc_wake = rtc_wake;
  in.time_ok = rtc_read_time(&in.time) && rtc_time_valid(&in.time);
  in.vbat = read_battery_mv();
  in.vbat_pulse = in.vbat;
  in.sensor_raw = 0;
  in.sensor_valid = false;

  if (g_state.state != (uint8_t)SystemState::FAULT && in.time_ok) {
    SensorReading s =
        sensor_read((g_state.flags & FLAG_SENSOR_STEMMA) != 0);
    in.sensor_raw = s.raw;
    in.sensor_valid = s.valid;
    in.vbat_pulse = read_battery_mv();
  }

  return safety_apply(evaluate_cycle(&g_state, &in), &in.time);
}
