/*
  StateMachine.h — Boot, wake, sleep, and FAULT-clear sequencing.

  REASONING.md §5 state machine (NORMAL / FAULT / CALIBRATE / MANUAL)

  License: MIT (see root LICENSE)
*/

#pragma once

#include <Arduino.h>

#include "Config.h"
#include "Persist.h"
#include "Power.h"
#include "RTC_DS3231.h"
#include "Safety.h"
#include "Sensor.h"
#include "Utils.h"
#include "Valve.h"

extern volatile uint8_t g_wake_flag;
extern uint8_t g_immediate_wake_streak;

inline void sm_isr() { g_wake_flag = 1; }

inline void sm_attach_wakes() {
  // ATmega328P: only a LOW *level* on INT0/INT1 wakes PWR_DOWN (datasheet
  // 12.2.1). FALLING will not wake. Attach immediately before sleep with
  // INT/SQW already released; detach on wake so a still-low pin cannot
  // re-enter the ISR. REASONING.md §2.1, §2.4
  if (digitalRead(RTC_INT_PIN) != LOW) {
    attachInterrupt(digitalPinToInterrupt(RTC_INT_PIN), sm_isr, LOW);
  }
  if (digitalRead(USER_BUTTON_PIN) != LOW) {
    attachInterrupt(digitalPinToInterrupt(USER_BUTTON_PIN), sm_isr, LOW);
  }
}

inline void sm_detach_wakes() {
  detachInterrupt(digitalPinToInterrupt(RTC_INT_PIN));
  detachInterrupt(digitalPinToInterrupt(USER_BUTTON_PIN));
}

inline bool sm_try_clear_fault() {
  if (g_state.state != (uint8_t)SystemState::FAULT) {
    return false;
  }
  if (!button_held(BUTTON_HOLD_MS)) {
    return false;
  }
  bool use_stemma = (g_state.flags & FLAG_SENSOR_STEMMA) != 0;
  SensorReading s = sensor_read(use_stemma);
  if (!s.valid) {
    DEBUG_PRINTLN(F("FAULT clear refused (sensor)"));
    return false;
  }
  g_state.state = (uint8_t)SystemState::NORMAL;
  g_state.fault_reason = (uint8_t)FaultReason::NONE;
  g_state.sensor_fail_count = 0;
  g_state.no_response_count = 0;
  g_state.low_batt_streak = 0;
  persist_save();
  DEBUG_PRINTLN(F("FAULT cleared"));
  return true;
}

inline void sm_boot() {
  PersistLoadResult pr = persist_load();
  if (pr == PERSIST_FIRST_BOOT || pr == PERSIST_CORRUPT) {
    persist_save();
  }

  if (sensor_probe_stemma()) {
    g_state.flags = (uint8_t)(g_state.flags | FLAG_SENSOR_STEMMA);
    if (g_state.flags & FLAG_NEEDS_CAL) {
      g_state.cal.dry_min = STEMMA_DEFAULT_DRY;
      g_state.cal.wet_max = STEMMA_DEFAULT_WET;
    }
  } else {
    g_state.flags = (uint8_t)(g_state.flags & (uint8_t)~FLAG_SENSOR_STEMMA);
  }

  bool rtc_ok = rtc_present() && rtc_configure_vbat();
#if ENABLE_DEBUG_SERIAL && SET_RTC_FROM_BUILD
  if (rtc_ok && rtc_oscillator_stopped()) {
    rtc_ok = rtc_set_from_build() && rtc_configure_vbat();
  }
#else
  if (rtc_ok && rtc_oscillator_stopped()) {
    rtc_ok = false;
  }
#endif

  if (!rtc_ok && g_state.state != (uint8_t)SystemState::FAULT) {
    safety_enter_fault(FaultReason::RTC_LOST, 0, read_battery_mv(), 0);
  }

  (void)sm_try_clear_fault();
  persist_log(EventAction::BOOT, (FaultReason)g_state.fault_reason,
              g_state.last_moisture, g_state.last_vbat_mv, 0);
  persist_save();

  DEBUG_PRINT(F("boot state="));
  DEBUG_PRINT(g_state.state);
  DEBUG_PRINT(F(" fault="));
  DEBUG_PRINTLN(g_state.fault_reason);
}

inline void sm_prepare_sleep() {
  g_wake_flag = 0;
  (void)rtc_clear_alarm_flags();
  if (digitalRead(RTC_INT_PIN) == LOW) {
    i2c_bus_recover();
    (void)rtc_clear_alarm_flags();
  }
  // Stuck-low INT after a clear is a real RTC/bus fault. A normal alarm
  // releases the line here, so the streak must not count scheduled wakes.
  if (digitalRead(RTC_INT_PIN) == LOW) {
    if (g_immediate_wake_streak < 255) {
      g_immediate_wake_streak++;
    }
    if (g_immediate_wake_streak >= 5 &&
        g_state.state != (uint8_t)SystemState::FAULT) {
      safety_enter_fault(FaultReason::RTC_LOST, g_state.last_moisture,
                         g_state.last_vbat_mv, 0);
    }
  } else {
    g_immediate_wake_streak = 0;
  }
}

inline void sm_run(bool rtc_wake) {
  (void)sm_try_clear_fault();
  (void)safety_run_cycle(rtc_wake);
}
