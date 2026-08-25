/*
  Logic.h — Pure decision and integrity helpers (no hardware, no Arduino).

  Compiled into the firmware and into host unit tests (make test).
  REASONING.md §5 Layers 1–5, §6 CRC, §7 thresholds.

  License: MIT (see root LICENSE)
*/

#pragma once

#include <stdint.h>

#include "Types.h"

// CRC-16-CCITT-FALSE: init 0xFFFF, poly 0x1021, xorout 0.
// REASONING.md §6 (Integrity & atomicity strategy)
inline uint16_t crc16_ccitt(const uint8_t *data, uint16_t len) {
  uint16_t crc = 0xFFFF;
  for (uint16_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t b = 0; b < 8; b++) {
      if (crc & 0x8000U) {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      } else {
        crc = (uint16_t)(crc << 1);
      }
    }
  }
  return crc;
}

inline uint16_t persist_payload_crc(const PersistentState *s) {
  return crc16_ccitt(reinterpret_cast<const uint8_t *>(s),
                     (uint16_t)PERSIST_CRC_BYTES);
}

inline bool persist_crc_ok(const PersistentState *s) {
  return persist_payload_crc(s) == s->crc;
}

inline void persist_set_crc(PersistentState *s) {
  s->crc = persist_payload_crc(s);
}

inline bool persist_looks_unprogrammed(const PersistentState *s) {
  const uint8_t *p = reinterpret_cast<const uint8_t *>(s);
  for (uint16_t i = 0; i < (uint16_t)sizeof(PersistentState); i++) {
    if (p[i] != 0xFFU) {
      return false;
    }
  }
  return true;
}

// Map a raw reading into 0–100 % of the calibrated dry/wet span.
// Invalid span → 0 (fail-closed: do not water). REASONING.md §7
inline uint8_t moisture_percent(uint16_t raw, uint16_t dry, uint16_t wet,
                                bool higher_is_wetter) {
  if (higher_is_wetter) {
    if (wet <= dry) {
      return 0;
    }
    if (raw <= dry) {
      return 0;
    }
    if (raw >= wet) {
      return 100;
    }
    return (uint8_t)(((uint32_t)(raw - dry) * 100UL) / (uint32_t)(wet - dry));
  }
  if (dry <= wet) {
    return 0;
  }
  if (raw >= dry) {
    return 0;
  }
  if (raw <= wet) {
    return 100;
  }
  return (uint8_t)(((uint32_t)(dry - raw) * 100UL) / (uint32_t)(dry - wet));
}

inline uint8_t apply_threshold_offset(uint8_t base_pct, int8_t offset) {
  int16_t v = (int16_t)base_pct + (int16_t)offset;
  if (v < 5) {
    v = 5;
  }
  if (v > 90) {
    v = 90;
  }
  return (uint8_t)v;
}

// REASONING.md §5 Layer 3 (hysteresis)
inline WaterDecision decide_watering(uint8_t pct, uint8_t low_th,
                                     uint8_t high_th) {
  if (low_th >= high_th) {
    return WD_NONE;
  }
  if (pct <= low_th) {
    return WD_WATER;
  }
  if (pct >= high_th) {
    return WD_WET;
  }
  return WD_NONE;
}

inline bool raw_in_plausible_range(uint16_t raw, uint16_t lo, uint16_t hi) {
  return raw >= lo && raw <= hi;
}

inline bool delta_implausible(uint16_t now, uint16_t last, uint16_t max_delta,
                              bool have_last) {
  if (!have_last) {
    return false;
  }
  uint16_t d = (now > last) ? (uint16_t)(now - last) : (uint16_t)(last - now);
  return d > max_delta;
}

inline bool moisture_rose_enough(uint16_t before, uint16_t after,
                                 uint16_t min_delta, bool higher_is_wetter) {
  if (higher_is_wetter) {
    return after >= before && (uint16_t)(after - before) >= min_delta;
  }
  return before >= after && (uint16_t)(before - after) >= min_delta;
}

inline uint16_t average_trim_minmax(const uint16_t *s, uint8_t n) {
  if (n == 0) {
    return 0;
  }
  if (n < 3) {
    uint32_t sum = 0;
    for (uint8_t i = 0; i < n; i++) {
      sum += s[i];
    }
    return (uint16_t)(sum / n);
  }
  uint16_t mn = s[0];
  uint16_t mx = s[0];
  uint32_t sum = 0;
  for (uint8_t i = 0; i < n; i++) {
    sum += s[i];
    if (s[i] < mn) {
      mn = s[i];
    }
    if (s[i] > mx) {
      mx = s[i];
    }
  }
  sum -= mn;
  sum -= mx;
  return (uint16_t)(sum / (uint8_t)(n - 2));
}

// Coarse calendar check. Year > 2024 per REASONING.md §5 Layer 1.
inline bool rtc_time_valid(const CivilTime *t) {
  if (t->year < 2025 || t->year > 2099) {
    return false;
  }
  if (t->month < 1 || t->month > 12) {
    return false;
  }
  if (t->day < 1 || t->day > 31) {
    return false;
  }
  if (t->hour > 23 || t->minute > 59 || t->second > 59) {
    return false;
  }
  return true;
}

inline bool hour_in_span(uint8_t hour, uint8_t start, uint8_t end) {
  if (start == end) {
    return false;
  }
  if (start < end) {
    return hour >= start && hour < end;
  }
  return hour >= start || hour < end;
}

inline bool in_irrigation_window(uint8_t hour, uint8_t w1s, uint8_t w1e,
                                 uint8_t w2s, uint8_t w2e) {
  return hour_in_span(hour, w1s, w1e) || hour_in_span(hour, w2s, w2e);
}

inline bool battery_ok(uint16_t mv, uint16_t threshold) { return mv >= threshold; }

inline bool daily_budget_remaining(uint8_t used, uint8_t max_pulses) {
  return used < max_pulses;
}

inline uint8_t cap_pulse_ms(uint16_t ms, uint16_t max_ms) {
  if (ms < 10) {
    return 10;
  }
  if (ms > max_ms) {
    return (uint8_t)max_ms;
  }
  return (uint8_t)ms;
}

// Next of two daily alarms, strictly in the future (hour already consumed).
// REASONING.md §2.1 (calendar scheduling)
inline void next_daily_alarm(uint8_t hour, uint8_t t1h, uint8_t t2h,
                             uint8_t *out_hour, uint8_t *add_days) {
  uint8_t a = t1h;
  uint8_t b = t2h;
  if (b < a) {
    uint8_t tmp = a;
    a = b;
    b = tmp;
  }
  *add_days = 0;
  if (hour < a) {
    *out_hour = a;
    return;
  }
  if (hour < b && a != b) {
    *out_hour = b;
    return;
  }
  *out_hour = a;
  *add_days = 1;
}

inline void add_minutes(uint8_t hour, uint8_t minute, uint16_t add,
                        uint8_t *out_hour, uint8_t *out_minute,
                        uint8_t *add_days) {
  uint32_t total = (uint32_t)hour * 60UL + (uint32_t)minute + (uint32_t)add;
  *add_days = (uint8_t)(total / (24UL * 60UL));
  total %= (24UL * 60UL);
  *out_hour = (uint8_t)(total / 60UL);
  *out_minute = (uint8_t)(total % 60UL);
}

inline uint32_t pack_civil(const CivilTime *t) {
  return ((uint32_t)t->year << 16) | ((uint32_t)t->month << 12) |
         ((uint32_t)t->day << 7) | (uint32_t)t->hour;
}

inline uint8_t bcd_to_bin(uint8_t bcd) {
  return (uint8_t)((bcd >> 4) * 10 + (bcd & 0x0F));
}

inline uint8_t bin_to_bcd(uint8_t bin) {
  return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}
