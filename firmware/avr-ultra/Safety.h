/*
  Safety.h — Six-layer defense cycle (REASONING.md §5).

  Fail-closed: any failed check refuses watering; persistent failures enter
  FAULT and force a close pulse.

  License: MIT (see root LICENSE)
*/

#pragma once

#include "Config.h"
#include "Logic.h"
#include "Persist.h"
#include "Power.h"
#include "RTC_DS3231.h"
#include "Sensor.h"
#include "Types.h"
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

inline void safety_reset_daily_budget(const CivilTime *t) {
  if (g_state.last_water_day != t->day ||
      g_state.last_water_month != t->month) {
    g_state.water_pulses_today = 0;
    g_state.last_water_day = t->day;
    g_state.last_water_month = t->month;
  }
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

inline SafetyResult safety_run_cycle(bool rtc_wake) {
  SafetyResult out;
  out.watered = false;
  out.faulted = false;
  out.reason = FaultReason::NONE;

  if (g_state.state == (uint8_t)SystemState::FAULT) {
    valve_force_close(g_state.pulse_ms);
    out.faulted = true;
    out.reason = (FaultReason)g_state.fault_reason;
    CivilTime ft;
    if (rtc_read_time(&ft) && rtc_time_valid(&ft)) {
      (void)safety_schedule_next(&ft, false);
    }
    return out;
  }

  CivilTime t;
  uint32_t ts = 0;
  bool time_ok = rtc_read_time(&t) && rtc_time_valid(&t);
  if (time_ok) {
    ts = pack_civil(&t);
    safety_reset_daily_budget(&t);
  }

  uint16_t vbat = read_battery_mv();
  g_state.last_vbat_mv = vbat;

  // ----- Layer 1 -----
  if (!time_ok) {
    out.reason = FaultReason::RTC_LOST;
    safety_enter_fault(FaultReason::RTC_LOST, g_state.last_moisture, vbat, ts);
    out.faulted = true;
    return out;
  }

  if (!battery_ok(vbat, LOW_BATT_THRESHOLD_MV)) {
    g_state.flags = (uint8_t)(g_state.flags | FLAG_LOW_BATT);
    if (g_state.low_batt_streak < 255) {
      g_state.low_batt_streak++;
    }
    out.reason = FaultReason::LOW_BATTERY;
    persist_log(EventAction::REFUSE, FaultReason::LOW_BATTERY,
                g_state.last_moisture, vbat, ts);
    if (g_state.low_batt_streak >= LOW_BATT_FAULT_STREAK) {
      safety_enter_fault(FaultReason::LOW_BATTERY_PERSISTENT,
                         g_state.last_moisture, vbat, ts);
      out.faulted = true;
    } else {
      persist_save();
    }
    (void)safety_schedule_next(&t, false);
    return out;
  }
  g_state.low_batt_streak = 0;
  g_state.flags = (uint8_t)(g_state.flags & (uint8_t)~FLAG_LOW_BATT);

  bool absorbing =
      (g_state.flags & FLAG_AWAITING_ABSORPTION) != 0;

  if (!absorbing &&
      !in_irrigation_window(t.hour, g_state.w1_start, g_state.w1_end,
                            g_state.w2_start, g_state.w2_end)) {
    out.reason = FaultReason::OUTSIDE_WINDOW;
    persist_log(EventAction::REFUSE, FaultReason::OUTSIDE_WINDOW,
                g_state.last_moisture, vbat, ts);
    persist_save();
    (void)safety_schedule_next(&t, false);
    return out;
  }

  if (!absorbing &&
      !daily_budget_remaining(g_state.water_pulses_today,
                              g_state.max_pulses_day)) {
    out.reason = FaultReason::DAILY_BUDGET_EXCEEDED;
    persist_log(EventAction::REFUSE, FaultReason::DAILY_BUDGET_EXCEEDED,
                g_state.last_moisture, vbat, ts);
    persist_save();
    (void)safety_schedule_next(&t, false);
    return out;
  }

  // ----- Layer 2 -----
  bool use_stemma = (g_state.flags & FLAG_SENSOR_STEMMA) != 0;
  SensorReading s = sensor_read(use_stemma);
  bool have_last = (g_state.flags & FLAG_HAVE_LAST_MOISTURE) != 0;
  bool jump = delta_implausible(s.raw, g_state.last_moisture, SENSOR_MAX_JUMP,
                                have_last);
  if (!s.valid || jump) {
    if (g_state.sensor_fail_count < 255) {
      g_state.sensor_fail_count++;
    }
    out.reason = FaultReason::SENSOR_IMPLAUSIBLE;
    persist_log(EventAction::REFUSE, FaultReason::SENSOR_IMPLAUSIBLE, s.raw,
                vbat, ts);
    if (g_state.sensor_fail_count >= SENSOR_FAIL_STREAK) {
      safety_enter_fault(FaultReason::SENSOR_DEAD, s.raw, vbat, ts);
      out.faulted = true;
    } else {
      persist_save();
      (void)safety_schedule_next(&t, absorbing);
    }
    return out;
  }
  g_state.sensor_fail_count = 0;

  if (absorbing && !rtc_wake) {
    // Button or reset during absorption: do not treat as a Layer 5 timeout.
    // Re-arm the soak alarm in case the previous Alarm1 was lost.
    (void)safety_schedule_next(&t, true);
    persist_save();
    return out;
  }

  if (absorbing) {
    // ----- Layer 5 -----
    bool rose = moisture_rose_enough(g_state.last_moisture, s.raw,
                                     g_state.min_response_delta,
                                     MOISTURE_HIGHER_IS_WETTER);
    g_state.flags =
        (uint8_t)(g_state.flags & (uint8_t)~FLAG_AWAITING_ABSORPTION);
    g_state.last_moisture = s.raw;
    g_state.flags = (uint8_t)(g_state.flags | FLAG_HAVE_LAST_MOISTURE);
    if (!rose) {
      if (g_state.no_response_count < 255) {
        g_state.no_response_count++;
      }
      persist_log(EventAction::REFUSE, FaultReason::NO_RESPONSE_TO_WATER,
                  s.raw, vbat, ts);
      if (g_state.no_response_count >= NO_RESPONSE_FAULT_STREAK) {
        safety_enter_fault(FaultReason::NO_RESPONSE_TO_WATER, s.raw, vbat, ts);
        out.faulted = true;
        return out;
      }
    } else {
      g_state.no_response_count = 0;
      persist_log(EventAction::ABSORB_OK, FaultReason::WET_ENOUGH, s.raw, vbat,
                  ts);
    }
    persist_save();
    (void)safety_schedule_next(&t, false);
    out.reason = rose ? FaultReason::WET_ENOUGH : FaultReason::NO_RESPONSE_TO_WATER;
    return out;
  }

  // ----- Layer 3 -----
  uint8_t low = apply_threshold_offset(g_state.low_th_pct,
                                       g_state.cal.threshold_offset);
  uint8_t pct = moisture_percent(s.raw, g_state.cal.dry_min,
                                 g_state.cal.wet_max, MOISTURE_HIGHER_IS_WETTER);
  WaterDecision d = decide_watering(pct, low, g_state.high_th_pct);
  g_state.last_moisture = s.raw;
  g_state.flags = (uint8_t)(g_state.flags | FLAG_HAVE_LAST_MOISTURE);

  if (d != WD_WATER) {
    out.reason = FaultReason::WET_ENOUGH;
    persist_log(EventAction::REFUSE, FaultReason::WET_ENOUGH, s.raw, vbat, ts);
    persist_save();
    (void)safety_schedule_next(&t, false);
    return out;
  }

  // ----- Layer 4 -----
  vbat = read_battery_mv();
  g_state.last_vbat_mv = vbat;
  if (!battery_ok(vbat, LOW_BATT_THRESHOLD_MV)) {
    out.reason = FaultReason::LOW_BATTERY;
    persist_log(EventAction::REFUSE, FaultReason::LOW_BATTERY, s.raw, vbat, ts);
    persist_save();
    (void)safety_schedule_next(&t, false);
    return out;
  }

  valve_open(g_state.pulse_ms);
  if (g_state.water_pulses_today < 255) {
    g_state.water_pulses_today++;
  }
  g_state.flags = (uint8_t)(g_state.flags | FLAG_AWAITING_ABSORPTION);
  persist_log(EventAction::OPEN_PULSE, FaultReason::NONE, s.raw, vbat, ts);
  persist_save();
  (void)safety_schedule_next(&t, true);
  out.watered = true;
  return out;
}
