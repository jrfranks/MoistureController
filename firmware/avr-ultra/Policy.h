/*
  Policy.h — Pure persist-slot choice and six-layer cycle (no I/O).

  Firmware adapters in Persist.h / Safety.h apply the returned commands.
  Host tests compile this file directly. REASONING.md §5, §6

  License: MIT (see root LICENSE)
*/

#pragma once

#include "Config.h"
#include "Logic.h"
#include "Types.h"

inline bool persist_image_ok(const PersistentState *s) {
  return s->magic == PERSIST_MAGIC && s->version == PERSIST_VERSION &&
         persist_crc_ok(s);
}

inline void persist_init_defaults(PersistentState *s) {
  for (uint16_t i = 0; i < (uint16_t)sizeof(PersistentState); i++) {
    reinterpret_cast<uint8_t *>(s)[i] = 0;
  }
  s->magic = PERSIST_MAGIC;
  s->version = PERSIST_VERSION;
  s->state = (uint8_t)SystemState::NORMAL;
  s->fault_reason = (uint8_t)FaultReason::NONE;
  s->flags = FLAG_NEEDS_CAL;
  s->cal.dry_min = DEFAULT_DRY_MIN;
  s->cal.wet_max = DEFAULT_WET_MAX;
  s->pulse_ms = DEFAULT_VALVE_PULSE_MS;
  s->absorption_min = DEFAULT_ABSORPTION_MINUTES;
  s->max_pulses_day = DEFAULT_MAX_PULSES_PER_DAY;
  s->w1_start = DEFAULT_W1_START_H;
  s->w1_end = DEFAULT_W1_END_H;
  s->w2_start = DEFAULT_W2_START_H;
  s->w2_end = DEFAULT_W2_END_H;
  s->low_th_pct = DEFAULT_LOW_TH_PCT;
  s->high_th_pct = DEFAULT_HIGH_TH_PCT;
  s->min_response_delta = DEFAULT_MIN_RESPONSE_DELTA;
  persist_set_crc(s);
}

enum PersistSlot : uint8_t {
  PERSIST_SLOT_PRIMARY = 0,
  PERSIST_SLOT_BACKUP = 1,
  PERSIST_SLOT_BLANK = 2,
  PERSIST_SLOT_CORRUPT = 3
};

inline PersistSlot persist_pick(const PersistentState *primary,
                               const PersistentState *backup) {
  if (persist_image_ok(primary)) {
    return PERSIST_SLOT_PRIMARY;
  }
  if (persist_image_ok(backup)) {
    return PERSIST_SLOT_BACKUP;
  }
  if (persist_looks_unprogrammed(primary) &&
      persist_looks_unprogrammed(backup)) {
    return PERSIST_SLOT_BLANK;
  }
  return PERSIST_SLOT_CORRUPT;
}

inline uint8_t persist_next_log_head(uint8_t head) {
  return (uint8_t)(((head % EVENT_LOG_SLOTS) + 1) % EVENT_LOG_SLOTS);
}

enum CycleCommand : uint8_t {
  CMD_HOLD_FAULT = 0,
  CMD_FAULT = 1,
  CMD_REFUSE = 2,
  CMD_OPEN = 3,
  CMD_SKIP_SOAK = 4
};

struct CycleView {
  bool time_ok;
  CivilTime time;
  uint16_t vbat;
  uint16_t vbat_pulse;
  uint16_t sensor_raw;
  bool sensor_valid;
  bool rtc_wake;
};

struct CycleOutcome {
  CycleCommand cmd;
  FaultReason reason;
  EventAction log_action;
  bool log;
  bool persist;
  bool schedule;
  bool schedule_soak;
  uint16_t moisture;
  uint16_t vbat;
  uint32_t timestamp;
};

inline void cycle_reset_daily_budget(PersistentState *st, const CivilTime *t) {
  if (st->last_water_day != t->day || st->last_water_month != t->month) {
    st->water_pulses_today = 0;
    st->last_water_day = t->day;
    st->last_water_month = t->month;
  }
}

inline CycleOutcome cycle_make(CycleCommand cmd, FaultReason reason,
                               uint16_t moisture, uint16_t vbat, uint32_t ts) {
  CycleOutcome o;
  o.cmd = cmd;
  o.reason = reason;
  o.log_action = EventAction::REFUSE;
  o.log = false;
  o.persist = false;
  o.schedule = false;
  o.schedule_soak = false;
  o.moisture = moisture;
  o.vbat = vbat;
  o.timestamp = ts;
  return o;
}

inline CycleOutcome cycle_fault(PersistentState *st, FaultReason r,
                               uint16_t moisture, uint16_t vbat, uint32_t ts) {
  st->state = (uint8_t)SystemState::FAULT;
  st->fault_reason = (uint8_t)r;
  CycleOutcome o = cycle_make(CMD_FAULT, r, moisture, vbat, ts);
  o.log_action = EventAction::FAULT;
  o.log = true;
  o.persist = true;
  o.schedule = (r != FaultReason::RTC_LOST);
  return o;
}

inline CycleOutcome cycle_refuse(FaultReason r, uint16_t moisture, uint16_t vbat,
                                uint32_t ts, bool soak) {
  CycleOutcome o = cycle_make(CMD_REFUSE, r, moisture, vbat, ts);
  o.log = true;
  o.persist = true;
  o.schedule = true;
  o.schedule_soak = soak;
  return o;
}

inline CycleOutcome evaluate_cycle(PersistentState *st, const CycleView *in) {
  const uint32_t ts = in->time_ok ? pack_civil(&in->time) : 0;

  if (st->state == (uint8_t)SystemState::FAULT) {
    CycleOutcome o =
        cycle_make(CMD_HOLD_FAULT, (FaultReason)st->fault_reason,
                   st->last_moisture, in->vbat, ts);
    o.schedule = in->time_ok;
    return o;
  }

  if (in->time_ok) {
    cycle_reset_daily_budget(st, &in->time);
  }
  st->last_vbat_mv = in->vbat;

  if (!in->time_ok) {
    return cycle_fault(st, FaultReason::RTC_LOST, st->last_moisture, in->vbat,
                       ts);
  }

  if (!battery_ok(in->vbat, LOW_BATT_THRESHOLD_MV)) {
    st->flags = (uint8_t)(st->flags | FLAG_LOW_BATT);
    if (st->low_batt_streak < 255) {
      st->low_batt_streak++;
    }
    if (st->low_batt_streak >= LOW_BATT_FAULT_STREAK) {
      return cycle_fault(st, FaultReason::LOW_BATTERY_PERSISTENT,
                         st->last_moisture, in->vbat, ts);
    }
    return cycle_refuse(FaultReason::LOW_BATTERY, st->last_moisture, in->vbat,
                        ts, false);
  }
  st->low_batt_streak = 0;
  st->flags = (uint8_t)(st->flags & (uint8_t)~FLAG_LOW_BATT);

  const bool absorbing = (st->flags & FLAG_AWAITING_ABSORPTION) != 0;

  if (!absorbing &&
      !in_irrigation_window(in->time.hour, st->w1_start, st->w1_end,
                            st->w2_start, st->w2_end)) {
    return cycle_refuse(FaultReason::OUTSIDE_WINDOW, st->last_moisture, in->vbat,
                        ts, false);
  }

  if (!absorbing &&
      !daily_budget_remaining(st->water_pulses_today, st->max_pulses_day)) {
    return cycle_refuse(FaultReason::DAILY_BUDGET_EXCEEDED, st->last_moisture,
                        in->vbat, ts, false);
  }

  const bool have_last = (st->flags & FLAG_HAVE_LAST_MOISTURE) != 0;
  const bool jump = delta_implausible(in->sensor_raw, st->last_moisture,
                                      SENSOR_MAX_JUMP, have_last);
  if (!in->sensor_valid || jump) {
    if (st->sensor_fail_count < 255) {
      st->sensor_fail_count++;
    }
    if (st->sensor_fail_count >= SENSOR_FAIL_STREAK) {
      return cycle_fault(st, FaultReason::SENSOR_DEAD, in->sensor_raw, in->vbat,
                         ts);
    }
    return cycle_refuse(FaultReason::SENSOR_IMPLAUSIBLE, in->sensor_raw,
                        in->vbat, ts, absorbing);
  }
  st->sensor_fail_count = 0;

  if (absorbing && !in->rtc_wake) {
    CycleOutcome o =
        cycle_make(CMD_SKIP_SOAK, FaultReason::NONE, st->last_moisture,
                   in->vbat, ts);
    o.persist = true;
    o.schedule = true;
    o.schedule_soak = true;
    return o;
  }

  if (absorbing) {
    const bool rose = moisture_rose_enough(st->last_moisture, in->sensor_raw,
                                           st->min_response_delta,
                                           MOISTURE_HIGHER_IS_WETTER);
    st->flags = (uint8_t)(st->flags & (uint8_t)~FLAG_AWAITING_ABSORPTION);
    st->last_moisture = in->sensor_raw;
    st->flags = (uint8_t)(st->flags | FLAG_HAVE_LAST_MOISTURE);
    if (!rose) {
      if (st->no_response_count < 255) {
        st->no_response_count++;
      }
      if (st->no_response_count >= NO_RESPONSE_FAULT_STREAK) {
        return cycle_fault(st, FaultReason::NO_RESPONSE_TO_WATER, in->sensor_raw,
                           in->vbat, ts);
      }
      return cycle_refuse(FaultReason::NO_RESPONSE_TO_WATER, in->sensor_raw,
                          in->vbat, ts, false);
    }
    st->no_response_count = 0;
    CycleOutcome o = cycle_refuse(FaultReason::WET_ENOUGH, in->sensor_raw,
                                  in->vbat, ts, false);
    o.log_action = EventAction::ABSORB_OK;
    return o;
  }

  const uint8_t low =
      apply_threshold_offset(st->low_th_pct, st->cal.threshold_offset);
  const uint8_t pct = moisture_percent(in->sensor_raw, st->cal.dry_min,
                                       st->cal.wet_max, MOISTURE_HIGHER_IS_WETTER);
  st->last_moisture = in->sensor_raw;
  st->flags = (uint8_t)(st->flags | FLAG_HAVE_LAST_MOISTURE);
  if (decide_watering(pct, low, st->high_th_pct) != WD_WATER) {
    return cycle_refuse(FaultReason::WET_ENOUGH, in->sensor_raw, in->vbat, ts,
                        false);
  }

  st->last_vbat_mv = in->vbat_pulse;
  if (!battery_ok(in->vbat_pulse, LOW_BATT_THRESHOLD_MV)) {
    return cycle_refuse(FaultReason::LOW_BATTERY, in->sensor_raw, in->vbat_pulse,
                        ts, false);
  }

  if (st->water_pulses_today < 255) {
    st->water_pulses_today++;
  }
  st->flags = (uint8_t)(st->flags | FLAG_AWAITING_ABSORPTION);
  CycleOutcome o =
      cycle_make(CMD_OPEN, FaultReason::NONE, in->sensor_raw, in->vbat_pulse, ts);
  o.log_action = EventAction::OPEN_PULSE;
  o.log = true;
  o.persist = true;
  o.schedule = true;
  o.schedule_soak = true;
  return o;
}
