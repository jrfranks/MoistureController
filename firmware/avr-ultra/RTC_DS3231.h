/*
  RTC_DS3231.h — Minimal DS3231 driver: VBAT-friendly control, alarm on INT.

  No third-party RTC library. TWI is powered only while talking to the chip.
  REASONING.md §2.1, §5 Layer 1

  License: MIT (see root LICENSE)
*/

#pragma once

#include <Arduino.h>
#include <Wire.h>

#include "Config.h"
#include "Logic.h"
#include "Utils.h"

#define DS3231_REG_SEC 0x00
#define DS3231_REG_CTRL 0x0E
#define DS3231_REG_STAT 0x0F
#define DS3231_REG_A1SEC 0x07

#define DS3231_CTRL_INTCN 0x04
#define DS3231_CTRL_A1IE 0x01
#define DS3231_STAT_A1F 0x01
#define DS3231_STAT_A2F 0x02
#define DS3231_STAT_EN32KHZ 0x08
#define DS3231_STAT_OSF 0x80

inline bool rtc_read_regs(uint8_t reg, uint8_t *data, uint8_t n) {
  twi_on();
  bool ok = i2c_read_reg(DS3231_I2C_ADDR, reg, data, n);
  if (!ok) {
    i2c_bus_recover();
    twi_on();
    ok = i2c_read_reg(DS3231_I2C_ADDR, reg, data, n);
  }
  twi_off();
  return ok;
}

inline bool rtc_write_regs(uint8_t reg, const uint8_t *data, uint8_t n) {
  twi_on();
  bool ok = i2c_write_reg(DS3231_I2C_ADDR, reg, data, n);
  if (!ok) {
    i2c_bus_recover();
    twi_on();
    ok = i2c_write_reg(DS3231_I2C_ADDR, reg, data, n);
  }
  twi_off();
  return ok;
}

inline bool rtc_write_u8(uint8_t reg, uint8_t v) {
  return rtc_write_regs(reg, &v, 1);
}

inline bool rtc_present() {
  uint8_t s = 0;
  return rtc_read_regs(DS3231_REG_STAT, &s, 1);
}

inline bool rtc_oscillator_stopped() {
  uint8_t s = 0;
  if (!rtc_read_regs(DS3231_REG_STAT, &s, 1)) {
    return true;
  }
  return (s & DS3231_STAT_OSF) != 0;
}

inline bool rtc_read_time(CivilTime *t) {
  uint8_t r[7];
  if (!rtc_read_regs(DS3231_REG_SEC, r, 7)) {
    return false;
  }
  t->second = bcd_to_bin(r[0] & 0x7F);
  t->minute = bcd_to_bin(r[1] & 0x7F);
  t->hour = rtc_decode_hour(r[2]);
  t->day = bcd_to_bin(r[4] & 0x3F);
  t->month = bcd_to_bin(r[5] & 0x1F);
  t->year = (uint16_t)(2000 + bcd_to_bin(r[6]));
  return true;
}

inline bool rtc_set_time(const CivilTime *t) {
  uint8_t r[7];
  r[0] = bin_to_bcd(t->second);
  r[1] = bin_to_bcd(t->minute);
  r[2] = bin_to_bcd(t->hour);
  r[3] = 1; // day-of-week unused
  r[4] = bin_to_bcd(t->day);
  r[5] = bin_to_bcd(t->month);
  r[6] = bin_to_bcd((uint8_t)(t->year - 2000));
  if (!rtc_write_regs(DS3231_REG_SEC, r, 7)) {
    return false;
  }
  uint8_t s = 0;
  if (!rtc_read_regs(DS3231_REG_STAT, &s, 1)) {
    return false;
  }
  s = (uint8_t)(s & (uint8_t)~(DS3231_STAT_OSF | DS3231_STAT_EN32KHZ |
                               DS3231_STAT_A1F | DS3231_STAT_A2F));
  return rtc_write_u8(DS3231_REG_STAT, s);
}

inline bool rtc_clear_alarm_flags() {
  uint8_t s = 0;
  if (!rtc_read_regs(DS3231_REG_STAT, &s, 1)) {
    return false;
  }
  s = (uint8_t)(s & (uint8_t)~(DS3231_STAT_A1F | DS3231_STAT_A2F |
                               DS3231_STAT_EN32KHZ));
  return rtc_write_u8(DS3231_REG_STAT, s);
}

// Alarm 1 matches HH:MM:SS, ignoring date so "tomorrow 05:00" is just 05:00.
inline bool rtc_set_alarm1(uint8_t hour, uint8_t minute, uint8_t second) {
  uint8_t a[4];
  a[0] = bin_to_bcd(second);           // A1M1 = 0
  a[1] = bin_to_bcd(minute);           // A1M2 = 0
  a[2] = bin_to_bcd(hour);             // A1M3 = 0
  a[3] = (uint8_t)(0x80);              // A1M4 = 1, ignore day
  if (!rtc_write_regs(DS3231_REG_A1SEC, a, 4)) {
    return false;
  }
  uint8_t ctrl = (uint8_t)(DS3231_CTRL_INTCN | DS3231_CTRL_A1IE);
  if (!rtc_write_u8(DS3231_REG_CTRL, ctrl)) {
    return false;
  }
  return rtc_clear_alarm_flags();
}

inline bool rtc_configure_vbat() {
  // Oscillator on (EOSC=0), INT pin (not square wave), Alarm1 interrupt.
  // 32 kHz pin off. REASONING.md §2.1
  uint8_t ctrl = (uint8_t)(DS3231_CTRL_INTCN | DS3231_CTRL_A1IE);
  if (!rtc_write_u8(DS3231_REG_CTRL, ctrl)) {
    return false;
  }
  return rtc_clear_alarm_flags();
}

#if ENABLE_DEBUG_SERIAL && SET_RTC_FROM_BUILD
inline uint8_t rtc_parse_build_month() {
  const char m[4] = {__DATE__[0], __DATE__[1], __DATE__[2], 0};
  if (m[0] == 'J' && m[1] == 'a') return 1;
  if (m[0] == 'F') return 2;
  if (m[0] == 'M' && m[2] == 'r') return 3;
  if (m[0] == 'A' && m[1] == 'p') return 4;
  if (m[0] == 'M' && m[2] == 'y') return 5;
  if (m[0] == 'J' && m[1] == 'u' && m[2] == 'n') return 6;
  if (m[0] == 'J' && m[1] == 'u') return 7;
  if (m[0] == 'A') return 8;
  if (m[0] == 'S') return 9;
  if (m[0] == 'O') return 10;
  if (m[0] == 'N') return 11;
  return 12;
}

inline bool rtc_set_from_build() {
  CivilTime t;
  t.year = (uint16_t)((__DATE__[7] - '0') * 1000 + (__DATE__[8] - '0') * 100 +
                      (__DATE__[9] - '0') * 10 + (__DATE__[10] - '0'));
  t.month = rtc_parse_build_month();
  t.day = (uint8_t)(((__DATE__[4] == ' ') ? 0 : (__DATE__[4] - '0')) * 10 +
                    (__DATE__[5] - '0'));
  t.hour = (uint8_t)((__TIME__[0] - '0') * 10 + (__TIME__[1] - '0'));
  t.minute = (uint8_t)((__TIME__[3] - '0') * 10 + (__TIME__[4] - '0'));
  t.second = (uint8_t)((__TIME__[6] - '0') * 10 + (__TIME__[7] - '0'));
  return rtc_set_time(&t);
}
#endif
