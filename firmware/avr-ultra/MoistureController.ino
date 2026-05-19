/*
  MoistureController.ino — Ultra low-power AVR firmware (Milestone 0 skeleton)

  Purpose of this slice:
  - Prove the single most important robustness property (Layer 0) on every
  reset.
  - Establish the correct lowest-leakage pin + peripheral configuration.
  - Provide a clean, traceable foundation that later milestones build upon
    without ever violating the "always close on boot" guarantee.

  What this sketch does on every power-up / reset / upload:
  1. Immediately drives the two H-bridge control pins to the safe de-energized
     state (both LOW). This is the hardware equivalent of "force valve closed".
  2. Calls power_init_lowest_leakage() which turns off every peripheral and
     configures all GPIOs for minimum leakage in SLEEP_MODE_PWR_DOWN.
  3. Enters deep sleep. The MCU will remain there until a hardware reset.

  No DS3231, no valve pulsing logic, no sensor reads, no EEPROM, no state
  machine. Those come in later milestones.

  Compile with Arduino IDE: open the firmware/avr-ultra/ folder, select
  Arduino Nano / Pro Mini / Uno (ATmega328P), 3.3 V if your board supports it,
  compile & upload. Observe with multimeter or logic analyzer that the valve
  pins go low within microseconds of reset and stay low.

  Debug Serial: completely disabled by default (see Config.h
  ENABLE_DEBUG_SERIAL). Turning it on for development adds significant leakage
  and is only for bring-up on a bench supply.

  All significant statements carry // REASONING.md §X.Y traceability.

  License: MIT (see root LICENSE)
*/

#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/wdt.h> // WDT safety + early disable for power (see setup + Power.h)

#include "Config.h"
#include "Power.h"
#include "Types.h"

// =============================================================================
// setup — runs on every reset / power-up / upload
// =============================================================================
void setup() {
  // ------------------------------------------------------------------
  // LAYER 0 — THE MOST CRITICAL SAFETY ACTION
  // ------------------------------------------------------------------
  // On ANY reset (power-on, brown-out, WDT, software, upload) we MUST
  // immediately place the latching valve driver into the safe "no current"
  // state BEFORE touching any other peripheral, reading config, or enabling
  // any rail.
  //
  // For a single-coil latching solenoid driven by H-bridge:
  //   Both IN1 and IN2 LOW → both sides of coil at same potential → 0 V,
  //   0 A, valve stays in whatever mechanical state it was left in (or
  //   the hardware "manual close" RC network can still fire).
  //
  // This line (and the two digitalWrites) are the reason the whole project
  // exists. Nothing may ever execute before them.
  // REASONING.md §5 (Layer 0 — Hardware & Boot Guarantees)
  // REASONING.md §5 (F3: Valve stuck open after "close" pulse)
  pinMode(VALVE_IN1_PIN, OUTPUT);
  digitalWrite(VALVE_IN1_PIN, LOW);
  pinMode(VALVE_IN2_PIN, OUTPUT);
  digitalWrite(VALVE_IN2_PIN, LOW);

  // Belt-and-suspenders WDT disable as early as possible (covers any WDT
  // left enabled by fuses or the bootloader). Primary disable lives in
  // power_init_lowest_leakage().
  // REASONING.md §2.4 (WDT completely disabled during sleep)
  wdt_disable();

  // ------------------------------------------------------------------
  // Now that the valve driver cannot energize the coil, configure the rest
  // of the chip for the lowest possible sleep current.
  // ------------------------------------------------------------------
  // REASONING.md §2.4 (MCU and Sleep Technique)
  // REASONING.md §3 (expected 0.1–0.5 µA MCU sleep)
  power_init_lowest_leakage();

  // Optional debug output is guarded and OFF by default for power reasons.
  // Never enable in a deployed solar/battery Ultra unit.
  // REASONING.md §3
#if ENABLE_DEBUG_SERIAL
  DEBUG_BEGIN();
  DEBUG_PRINTLN(F("Ultra M0 boot: Layer 0 close applied, pins low leakage"));
#endif

  // Future milestones will:
  //   - read persisted state from EEPROM (with CRC)
  //   - init RTC in VBAT mode and clear alarm flags
  //   - decide whether we are in FAULT and must stay closed
  //   - etc.
  //
  // For Milestone 0 we stop here and prove the sleep configuration.
}

// =============================================================================
// loop — extremely thin in all versions
// =============================================================================
void loop() {
  // In a real scheduled system we would only reach meaningful work after
  // an RTC alarm woke us. Milestone 0 has no wake source configured yet,
  // so after setup() we simply demonstrate correct deep sleep entry.
  //
  // The MCU will remain in PWR_DOWN until a reset or (later) an external
  // interrupt on the RTC_INT_PIN.
  // REASONING.md §2.4
  // REASONING.md §2.1 (future DS3231 alarm wake)
  power_enter_deep_sleep();

  // When we wake in later milestones, execution resumes here.
  // We will then run the full 6-layer safety cycle and go back to sleep.
  // For M0 this line is never reached in normal operation.
}

// =============================================================================
// End of MoistureController.ino (Milestone 0)
// Every line above exists only to guarantee Layer 0 and minimal leakage.
// =============================================================================
