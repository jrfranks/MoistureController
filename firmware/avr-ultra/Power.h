/*
  Power.h — Power rail control + lowest-leakage pin configuration for Ultra
  target.

  This module owns every decision that affects quiescent current.
  It is called first in setup() and before every deep sleep.

  All AVR register manipulation is deliberate and documented.
  No external libraries beyond core avr/ headers.

  Arduino sketch folder layout: implementation lives here (no .cpp in v0
  skeleton).

  License: MIT (see root LICENSE)
*/

#pragma once

#include <avr/io.h>
#include <avr/power.h>
#include <avr/sleep.h>
#include <avr/wdt.h>
#include <stdint.h>

#include "Config.h"

// =============================================================================
// power_init_lowest_leakage
// =============================================================================
// Configures the entire GPIO, PRR, DIDR0, comparator, WDT, and pull-ups for the
// absolute minimum sleep current on a genuine ATmega328P in
// SLEEP_MODE_PWR_DOWN. Expected MCU contribution: 0.1–0.5 µA (datasheet + real
// silicon at 3.3 V).
//
// Must be called on every boot and before entering sleep.
//
// REASONING.md §2.4 — MCU and Sleep Technique (Genuine ATmega328P, PWR_DOWN,
// Everything Off) REASONING.md §3 — System Power Budget (ATmega328P (PWR_DOWN)
// 0.1–0.5 µA target) REASONING.md §2.3 — DIDR0 + GPIO configured as outputs
// driving low (lowest leakage)
void power_init_lowest_leakage() {
  // ------------------------------------------------------------------
  // 1. Power Reduction Register + WDT — disable every peripheral we do not need
  // ------------------------------------------------------------------
  // PRR = 0xFF turns off: TWI, TIM2, TIM0, TIM1, SPI, USART0, ADC.
  // wdt_disable() is mandatory per the "WDT completely disabled during sleep"
  // requirement (otherwise it can draw current and cause spurious resets).
  // This alone saves hundreds of µA vs. default.
  // REASONING.md §2.4
  PRR = 0xFF;
  wdt_disable(); // REASONING.md §2.4 — required for power budget and
                 // "Everything Off"

  // ------------------------------------------------------------------
  // 2. Disable analog comparator (saves ~10–50 µA if left on)
  // ------------------------------------------------------------------
  ACSR |= (1 << ACD);

  // ------------------------------------------------------------------
  // 3. Disable digital input buffers on ALL ADC pins (A0–A5)
  // ------------------------------------------------------------------
  // Prevents leakage through the input protection diodes when pins are
  // driven externally or floating. Critical for sensor pins and vbat sense.
  // REASONING.md §2.3
  DIDR0 = 0x3F; // bits 0-5 = ADC0..ADC5

  // ------------------------------------------------------------------
  // 4. GPIO configuration for minimum leakage
  // ------------------------------------------------------------------
  // Strategy proven on real hardware for sub-µA sleep:
  //   - All unused pins → OUTPUT + LOW (eliminates floating input leakage)
  //   - Future input pins (RTC_INT) → INPUT + explicit no-pull-up
  //   - Power gate control pins → OUTPUT + HIGH (P-MOS off = rails dead)
  //   - Valve H-bridge inputs → OUTPUT + LOW (no coil current, safe state)
  //
  // Pins 0-19 cover the full ATmega328P port space on Nano/Uno/Pro Mini.
  // We intentionally drive everything; specific modules (Valve/Sensor) will
  // reconfigure their pins when they are actually enabled later.
  // REASONING.md §2.4 and §3
  for (uint8_t p = 0; p < 20; ++p) {
    if (p == RTC_INT_PIN) {
      // RTC open-drain interrupt line: input, no pull-up (RTC pulls low when
      // active)
      pinMode(p, INPUT);
      digitalWrite(p, LOW); // writing LOW with DDR=0 clears pull-up
    } else if (p == SENSOR_POWER_GATE_PIN || p == VALVE_POWER_GATE_PIN) {
      // P-MOS gates: drive HIGH to keep rails completely disconnected
      pinMode(p, OUTPUT);
      digitalWrite(p, HIGH);
    } else if (p == VALVE_IN1_PIN || p == VALVE_IN2_PIN) {
      // H-bridge legs: both LOW = zero voltage across coil = safe de-energized
      // (Layer 0 close state is established in setup() before this call)
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
    } else {
      // Everything else: output low for lowest leakage
      pinMode(p, OUTPUT);
      digitalWrite(p, LOW);
    }
  }

  // ------------------------------------------------------------------
  // 5. Additional AVR power tweaks
  // ------------------------------------------------------------------
  // Disable pull-ups on all ports explicitly (belt-and-suspenders after the
  // loop) PORTB = PORTC = PORTD = 0; is already achieved by the
  // digitalWrite(LOW) above when pins were outputs; for any that became inputs
  // we wrote 0.
}

// =============================================================================
// sensor_rail_enable
// =============================================================================
// Turns the sensor power rail on or off via its P-MOS high-side switch.
// on=true  → gate LOW  → P-MOS conducts → sensor sees VCC (only for ms)
// on=false → gate HIGH → P-MOS off      → 0 µA through sensor
// REASONING.md §2.3 (Sensor Power Strategy — Mandatory Power Gating)
void sensor_rail_enable(bool on) {
  pinMode(SENSOR_POWER_GATE_PIN, OUTPUT);
  digitalWrite(SENSOR_POWER_GATE_PIN, on ? LOW : HIGH);
}

// =============================================================================
// valve_rail_enable
// =============================================================================
// Powers the H-bridge VM pin (and therefore the valve coil supply) only
// for the few tens of milliseconds required for a latching pulse.
// The bulk capacitor supplies the actual current; this rail is off 99.999 %
// of the time.
// REASONING.md §2.2 and §10 (Power Architecture)
void valve_rail_enable(bool on) {
  pinMode(VALVE_POWER_GATE_PIN, OUTPUT);
  digitalWrite(VALVE_POWER_GATE_PIN, on ? LOW : HIGH);
}

// =============================================================================
// read_battery_mv (stub for Milestone 0)
// =============================================================================
// Returns battery voltage in millivolts using the resistor divider on A2.
// Real implementation will:
//   - enable ADC only for the read
//   - average several samples
//   - apply calibration factor for the exact divider ratio
// For now returns a plausible mid-charge value so upper layers can compile.
// REASONING.md §5 (Layer 1 + Layer 4 voltage checks)
uint16_t read_battery_mv() {
  // TODO(Milestone 1+): real ADC read with ADC noise reduction, etc.
  // For skeleton we just prove the call site exists and power config works.
  return 3850; // placeholder ~3.85 V (typical LiFePO4 resting)
}

// =============================================================================
// power_enter_deep_sleep
// =============================================================================
// Enters SLEEP_MODE_PWR_DOWN with BOD disabled.
// This is the only sleep mode that achieves the target MCU current.
// Caller must have already called power_init_lowest_leakage().
// After wake (future RTC INT), execution resumes here; we then return
// so the sketch can decide what to do. In Milestone 0 there is no wake source
// configured — the MCU will stay asleep until a hardware reset.
// REASONING.md §2.4 (SLEEP_MODE_PWR_DOWN + sleep_bod_disable())
void power_enter_deep_sleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();

  // The critical line: BOD must be off during PWR_DOWN for sub-µA.
  // This must be done as close as possible to sleep_cpu().
  // REASONING.md §2.4
  sleep_bod_disable();

  // MCU stops here. Only reset, pin-change, or external INT can wake it.
  sleep_cpu();

  // ------------------------------------------------------------------
  // Execution resumes here after an interrupt (when we add RTC later).
  // ------------------------------------------------------------------
  sleep_disable();

  // On real wake we would selectively re-enable only what is needed
  // (e.g. PRR bits for TWI if talking to DS3231). For Milestone 0 the
  // next thing that happens is a full reset or the caller re-inits.
}

// =============================================================================
// End of Power.h — every register write and pin choice is justified above
// =============================================================================
