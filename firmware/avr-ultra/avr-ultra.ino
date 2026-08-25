/*
  avr-ultra.ino — Ultra low-power AVR firmware (Milestone 1)

  Milestone 1 is a scheduled, fail-closed single-zone controller:
    Layer 0  — close-pulse the latching valve on every reset, before anything
               else that can use the coil.
    Layers 1–6 — RTC window / budget / battery, power-gated sensing, hysteresis
               decision, pulse, absorption check, CRC EEPROM + event log.
    Sleep    — SLEEP_MODE_PWR_DOWN, BOD off, WDT off, RTC Alarm1 on INT0
               (button on INT1 as a human wake).

  Milestone 0 only de-energized the H-bridge and slept forever. That is not
  Layer 0 for a bistable valve: an open latch stays open. This sketch pulses
  close first, then runs the safety cycle.

  Compile: `make ultra` or open firmware/avr-ultra/ in the Arduino IDE.
  Debug Serial is compile-time off (Config.h ENABLE_DEBUG_SERIAL).

  Traceability: // REASONING.md §X.Y on significant paths.

  License: MIT (see root LICENSE)
*/

#include <EEPROM.h>
#include <Wire.h>
#include <avr/wdt.h>

#include "Config.h"
#include "Persist.h"
#include "Power.h"
#include "StateMachine.h"
#include "Utils.h"
#include "Valve.h"

PersistentState g_state;
volatile uint8_t g_wake_flag = 0;
uint8_t g_immediate_wake_streak = 0;

void setup() {
  // WDT first so a bootloader-enabled watchdog cannot reset us mid-init.
  // Layer 0 pin-safe + close pulse is the next action and must still beat
  // Serial / I2C / EEPROM. REASONING.md §5 Layer 0, §2.4
  wdt_disable();
  valve_pins_safe();
  valve_force_close(DEFAULT_VALVE_PULSE_MS);

  power_init_lowest_leakage();

#if ENABLE_DEBUG_SERIAL
  DEBUG_BEGIN();
  DEBUG_PRINTLN(F("Ultra M1 boot"));
#endif

  sm_boot();
}

void loop() {
  sm_detach_wakes();
  // Sample wake source before any I2C that might release INT/SQW.
  const bool rtc_wake = digitalRead(RTC_INT_PIN) == LOW;
  sm_run(rtc_wake);
  sm_prepare_sleep();
  while (digitalRead(USER_BUTTON_PIN) == LOW) {
    busy_delay_ms(20);
  }
  sm_attach_wakes();
  // REASONING.md §2.4 (PWR_DOWN + BOD off). Wakes on RTC INT or button.
  power_enter_deep_sleep();
}
