# MoistureController — Design Reasoning and Trade-offs

**Project**: MoistureController — Production-grade, ultra-low-power, single-zone garden irrigation controller  
**Version of this document**: 1.0 (Initial)  
**Last major update**: 2025 (Phase 0)  
**Status**: Living document. This file is the single source of truth for *why* every decision was made.  
**Rule**: Any change to firmware, hardware, or documentation that affects power, safety, or architecture **must** update this file and add a code comment of the form `// REASONING.md §X.Y`.

---

## §1. Project Goals and Hard Constraints

The purpose of MoistureController is to create a **reliable, long-term, set-and-forget** automatic irrigation controller for home gardens, raised beds, and small plots that:

- Uses the **absolute minimum power** possible so it can run for many months (ideally multiple seasons) on a small battery + tiny solar panel without frequent intervention.
- Is **extremely robust against failure** — the system must default to a safe "valve closed" state under almost any conceivable fault (sensor failure, wiring break, power loss, software hang, RTC failure, memory corruption, etc.).
- Is fully open source, well documented, and reproducible by careful hobbyists and small makers.
- Supports both the simplest possible build (for beginners) and a high-reliability "Ultra" path.

**Non-negotiable hard constraints** (these filter every design choice):

1. **Lowest possible power consumption**  
   Target: **< 5–10 µA total system sleep current** (including RTC). This enables multi-month to multi-year operation on 18650, LiFePO4, or even CR123A/supercap + solar.

2. **Fully documented reasoning**  
   Every significant decision must be explained here with numbers, trade-offs, and references. Source code must cross-reference this document.

3. **Maximum robustness (defense-in-depth)**  
   We assume components **will** fail. The system must degrade gracefully to "valve forced closed, human intervention required" rather than silent flooding or dead plants. No single point of failure should cause uncontrolled watering.

These three constraints take precedence over convenience, cost, or "simplicity for beginners" when they conflict.

---

## §2. Architectural Trade-off Analysis

### 2.1 Wake Source — Why DS3231 VBAT Alarm (not WDT, not comparator)

**Current v2.2 baseline**: Uses AVR Watchdog Timer (WDT) in interrupt-only mode, ~8 s ticks, 38 ticks ≈ 5 min sleep. Simple, no extra parts.

**Previous design** (commit b3c075c): Analog comparator (`analogComp` library) comparing moisture sensor vs potentiometer on AIN0/AIN1. Async wake on threshold crossing + `LowPower.powerDown(SLEEP_FOREVER)`.

**Chosen for Ultra target**: DS3231 real-time clock running in **true VBAT battery-backup mode** (~0.84–1.0 µA typical, max ~3 µA) with Alarm 1/2 interrupt on the INT/SQW pin to wake the ATmega328P from `SLEEP_MODE_PWR_DOWN`.

**Reasoning** (power + robustness):

- WDT granularity is too coarse for intelligent irrigation. Watering at 03:00 or "only at dawn" is impossible. Average current is higher because the MCU wakes more often than necessary.
- Comparator wake is elegant and near-zero power while sleeping, but:
  - It reacted to every threshold crossing (including noise, temperature drift, or temporary surface moisture).
  - The library proved difficult to make robust (see commit 63da067 "lost interrupts").
  - No calendar or time-of-day awareness — cannot implement "water only during allowed windows" or multi-day drought logic.
  - Still requires the MCU to be awake to decide and act; the async advantage is smaller than it first appears once you add proper safety layers.
- DS3231 VBAT + alarm gives:
  - Precise, calendar-based scheduling (e.g., check at 05:00 and 19:00 local solar time, or once per day in winter).
  - Ability to sleep for **days** between checks when appropriate (huge power win).
  - Very low current (1–3 µA) when the main supply is removed and the chip runs from its coin cell or the main battery on VBAT.
  - Temperature compensation built-in (useful for long-term calibration drift).
  - A persistent, accurate timestamp for the event log (critical for post-mortem after faults).

**Trade-off accepted**: One extra I2C part (~$1–2) and slightly more complex bring-up. The power and robustness gains are decisive.

**Implementation notes**: Cheap ZS-042 modules require modification (remove power LED, disable charging circuit for the backup cell, run primarily from VBAT). These mods are documented in the hardware section.

### 2.2 Valve Technology — Why Latching (Bistable) Solenoid + H-Bridge

**Baseline assumption** (README + v2.2 code): Conventional 5 V normally-closed solenoid, held open for 10 seconds via MOSFET. High holding current (hundreds of mA while open).

**Chosen**: 9 V or 12 V **latching / bistable** irrigation solenoid (single-coil, polarity-reversing, 2-wire types common from Hunter, Rain Bird, Gardena, etc.). Short 30–100 ms pulse in one polarity to open, opposite polarity to close. **Zero holding current** after the pulse.

**Driver**: DRV8833 (or TB6612FNG, or discrete 4-MOSFET H-bridge) with large bulk capacitor (470–2200 µF) to supply the pulse surge.

**Why this wins on both power and robustness**:

- Energy per watering event drops from ~10 s × 400–800 mA (4–8 joules or more) to ~50 ms × 1–2 A pulse (0.05–0.2 joules). Two orders of magnitude less energy per actuation.
- Valve state survives power loss (it stays in the last commanded position). This is actually a robustness feature if we always command "close" on boot/reset.
- Enables true "absorption time" (minutes to hours) after watering without keeping any power rails alive — the valve is already closed or open as commanded.
- Modern latching valves are widely available and reasonably priced for irrigation use.

**Risks and mitigations**:
- Latching valves are less common in hobbyist stores than 5 V NC solenoids → We will document a "higher-power conventional path" as an explicitly worse (but simpler for beginners) alternative in the hardware docs, with clear power penalty warnings.
- Pulse must be reliable → Bulk cap + voltage monitoring before pulse + configurable pulse length + verification (where possible).
- Stuck valve → Still a risk. Mitigated by the 6-layer safety system (never water if previous watering produced no moisture change).

**Power gating the driver rail** (high-side P-MOSFET) is mandatory so the H-bridge and valve coil have zero quiescent current between pulses.

### 2.3 Sensor Power Strategy — Mandatory Power Gating

**Baseline**: Sensor VCC tied directly to 5 V / 3.3 V rail — always consuming a few hundred µA to low mA.

**Chosen**: Every sensor (analog or STEMMA) is powered through a **P-channel MOSFET load switch** (e.g., AO3401, Si2301CDS, or similar, Rds(on) < 100 mΩ, very low gate leakage). The rail is enabled only for the few milliseconds needed to take stable readings (typically 3–7 samples + discard outliers), then turned off completely.

**Why**:
- A typical cheap capacitive analog sensor draws 2–5 mA when powered. Left on 24/7 this is the dominant consumer in the system and destroys the < 10 µA target.
- Powering only during measurement reduces sensor energy to negligible levels and also reduces long-term electrolysis/corrosion risk inside the soil probe.
- The same technique works for the STEMMA (power its VIN pin).

**Additional**: Digital input buffers on the ADC pins are disabled (`DIDR0`) and pins are configured as outputs driving low during sleep to eliminate leakage.

### 2.4 MCU and Sleep Technique — Genuine ATmega328P, PWR_DOWN, Everything Off

**Chosen configuration for Ultra target**:
- Genuine Microchip ATmega328P (not counterfeit clones).
- 3.3 V operation, 8 MHz internal RC oscillator (or low-power crystal if required).
- `SLEEP_MODE_PWR_DOWN`.
- BOD disabled (fuses + `sleep_bod_disable()` called immediately before `sleep_cpu()`).
- WDT completely disabled during sleep (only used for short watchdog reset if needed).
- `PRR = 0xFF` (all peripherals powered down).
- All GPIO: outputs driving low (or inputs with pull-ups disabled) + `DIDR0 = 0xFF`.
- AREF pin has only a small capacitor to GND (no direct connection to VCC).

**Expected MCU sleep current**: 0.1–0.5 µA typical on genuine silicon at 3.3 V, room temperature.

**Why not Pro Mini / Nano as primary**:
- Onboard regulators, power LEDs, and USB-serial chips add hundreds of µA to low mA of leakage. They are acceptable for prototyping but must be removed or bypassed (cut traces, remove LED) for production Ultra builds. The KiCad design will have a "minimal" section that makes this easy.

### 2.5 Dual-Target Strategy — Ultra AVR vs Smart ESP32

We will maintain two parallel but logically consistent implementations:

- **Ultra (primary for power/robustness)**: ATmega328P + DS3231 + power gating + latching valve. No radio. Target < 5–10 µA sleep. Maximum reliability, minimum intervention.
- **Smart**: ESP32-C3 (or C6) + ESPHome or Arduino + MQTT / Home Assistant. Same safety state machine and rules. Sleep current will be higher (15–80 µA typical with good optimization) but still excellent. Gains: OTA, remote control, weather API integration, beautiful dashboards, multi-zone easier.

**Why both?**
- Many users want "it just works with my Home Assistant" and are willing to accept a small solar panel + larger battery.
- The pure AVR Ultra target proves what is possible at the absolute extreme of low power and will be the reference for correctness of the safety logic.
- Shared concepts (state machine, calibration, event logging, FAULT reasons) will be documented once in this file and re-implemented (or ported) in both targets.

### 2.6 Why the Previous Designs Were Abandoned for the New Target

- The comparator + LowPower design was elegant for power but insufficiently robust once real-world noise, debounce, and the need for calendar scheduling were considered.
- The v2.2 WDT + analogRead + 10 s water + 60 s wait design improved debuggability and removed library dependencies, but it cannot meet the power target and has weak failure detection (a bad sensor can cause repeated 10 s watering cycles every 5 minutes).

The new architecture (RTC-scheduled, power-gated, latching, 6-layer safety, persistent black-box log) is the first design that simultaneously satisfies all three hard constraints.

---

## §3. System Power Budget (Target < 5–10 µA Average Sleep)

All figures are for 3.3 V operation at ~20–25 °C unless noted. These are the numbers the hardware and firmware must achieve.

| Component                  | Sleep Current (typ)     | Notes / Source                                                                 | How Achieved |
|---------------------------|-------------------------|----------------------------------------------------------------------------------|--------------|
| ATmega328P (PWR_DOWN)     | 0.1 – 0.5 µA           | Microchip datasheet; genuine parts only; BOD off, WDT off, pins correct         | Fuses + `sleep_bod_disable()` + pin config |
| DS3231 (VBAT mode)        | 0.84 – 1.0 µA (max 3.5)| Analog Devices DS3231 datasheet; after ZS-042 mods (LED + charger removed)     | Run from VBAT, EN32kHz=0, minimal I2C |
| Power switch leakage (2×) | < 0.1 µA each          | Good P-MOSFET (AO3401 class) with gate pulled up                               | Proper gate drive + pull-up |
| H-bridge (DRV8833) Iq     | ~1–2 µA                | When VM and logic supplies are present but no activity                          | High-side power gate on the entire valve rail |
| Sensor (when gated off)   | 0 µA                   | Rail completely disconnected                                                   | P-MOSFET load switch |
| Bulk caps + misc leakage  | < 0.5 µA               | High-quality low-leakage electrolytics + layout                                | Careful PCB design |
| **Total system (sleep)**  | **~2 – 5 µA typical**  | —                                                                              | Sum of above + margin |

**Active energy budget (example)**:
- Sensor sample (5 ms @ 3 mA): negligible
- DS3231 alarm + I2C read + decision: ~50–200 µA for < 100 ms
- Latching pulse (50 ms @ 1.5 A from cap, not from battery directly): 75 mJ
- One watering event per day (2 pulses) costs < 0.2 J from the battery — easily supplied by a 5 V 100 mA solar panel even in winter.

**Validation requirement**: Every hardware revision must publish measured sleep current (with photos of the meter and unit in sleep) in the hardware documentation. Target < 10 µA at 3.3 V, 25 °C.

---

## §4. Failure Modes and Effects Analysis (FMEA)

This table drives the 6-layer defense design. Only the highest-severity items are shown here; the full living version will be expanded.

| ID | Failure Mode                        | Severity (1-10) | Likelihood | Primary Mitigation Layer(s)                  | Residual Risk & Notes |
|----|-------------------------------------|-----------------|------------|----------------------------------------------|-----------------------|
| F1 | Moisture sensor reads "dry" when actually wet (or disconnected) | 10 (flood)     | Medium    | Layer 2 (plausibility + range check), Layer 5 (post-water moisture rise validation), daily budget, FAULT after N failures | High if sensor dies in dry state |
| F2 | Moisture sensor reads "wet" when actually dry | 8 (plants die) | Medium    | Adaptive baseline + manual calibration trigger, multiple samples, temperature compensation | Lower impact |
| F3 | Valve stuck open after "close" pulse | 10 (flood)     | Low       | Layer 0 (close on every boot), Layer 6 (log), manual hardware close override, post-action validation | Critical — needs field testing |
| F4 | MCU hangs or infinite loop          | 9              | Low       | Hardware WDT (reset) + Layer 0 on boot, brown-out detection | WDT must be carefully configured |
| F5 | RTC loses time or alarm fails       | 7              | Medium    | Layer 1 checks for valid RTC time, fallback to longer WDT sleep if RTC bad, persistent "last successful schedule" | Graceful degradation |
| F6 | EEPROM corruption (power loss mid-write) | 8          | Medium    | CRC on every block, double-buffering / versioned slots, recovery to safe defaults + FAULT | Must be tested with power-cut harness |
| F7 | Battery too low to complete a pulse | 6              | High (winter) | Layer 1 + Layer 4 (voltage check immediately before pulse), low-batt persistent flag, refuse watering | Document expected runtime |
| F8 | Power loss while valve is open      | 7              | Low       | Latching valve (state survives), "manual close" RC network on PCB that can fire a close pulse from stored charge | Nice-to-have hardware feature |
| F9 | I2C bus lock-up (STEMMA or DS3231)  | 6              | Low       | I2C timeout + bus recovery (clock stretching, reset), fall back to analog sensor if present | — |
| F10| User mis-calibrates or places sensor badly | 7         | High      | Self-cal on first boot + easy re-cal button, good documentation, adaptive baseline over weeks | Education + good defaults |

The design rule: **Any single failure must not produce uncontrolled watering.** Multiple coincident failures are allowed to produce "valve closed + FAULT + log + human needed."

---

## §5. Robustness Architecture — 6-Layer Defense-in-Depth

The system is deliberately over-engineered for safety. Layers are checked in order on every wake.

### Layer 0 — Hardware & Boot Guarantees
- On any reset or power-up (including WDT reset), the firmware **immediately** sends a "close" pulse to the latching valve **before** reading configuration or sensors.
- A hardware "manual close" network (large capacitor + RC timer + MOSFET path that bypasses the MCU) is strongly recommended on the PCB so a completely dead MCU can still close the valve.
- Latching valve state survives total power loss.

### Layer 1 — Pre-Wake / Pre-Action Checks (Always)
- RTC time is valid (year > 2024, reasonable month/day). If not, refuse automatic watering and log `RTC_LOST`.
- Battery voltage (via resistor divider on ADC) above configurable `LOW_BATT_THRESHOLD`. If not, set persistent low-batt flag and refuse watering.
- Daily water budget (seconds or pulses) not exhausted.
- Current time is inside an allowed irrigation window (configurable "only between 04:00–08:00 and 18:00–22:00" or "only at civil dawn/dusk").
- Time since last successful watering > minimum off time.

Only if **all** Layer 1 checks pass do we proceed to sensing.

### Layer 2 — Sensor Acquisition and Validation
- Enable sensor power rail.
- Take 5–7 samples at 10–50 ms intervals.
- Discard highest and lowest, average the rest.
- Check the averaged value is within the calibrated [dry_min, wet_max] range ± guard bands.
- Check the value has not changed more than a configured maximum delta since the last successful reading (catches sudden disconnects or shorts).
- For STEMMA: also read temperature; apply simple linear compensation if desired.
- Disable sensor power rail.
- If validation fails N times consecutively → enter `FAULT` with reason `SENSOR_DEAD`.

### Layer 3 — Decision
- Compare validated moisture against current threshold (or adaptive baseline + offset).
- Apply hysteresis (e.g., "needs water" below 35 % of range, "sufficiently wet" above 55 %).
- Adaptive baseline slowly tracks the "field capacity" wet reading over days/weeks using a very slow exponential moving average. This compensates for sensor drift and seasonal soil changes.

### Layer 4 — Final Authorization + Action
- Re-measure battery voltage immediately before the pulse (voltage can sag under load).
- If any check fails, abort and log the specific reason.
- Issue the latching "open" pulse (configurable 30–120 ms, default 50 ms).
- Record exact timestamp, pre-water moisture, battery voltage, pulse length used.

### Layer 5 — Post-Action Validation & Absorption
- Wait the configured absorption time (minutes to hours — valve is already in the commanded state, zero extra power).
- Re-enable sensor, take fresh validated reading.
- If moisture rose by less than the configured "response threshold", increment "no_response_count".
- If no_response_count ≥ configurable M (default 3) → enter permanent `FAULT` with reason `NO_RESPONSE_TO_WATER`.
- Otherwise reset the counter and record success.

### Layer 6 — Logging, Persistence, and Fault Handling
- Every decision and action (including refusals) is written to a circular 16-event black-box log in EEPROM with CRC.
- On entering `FAULT`, the reason code, timestamp, and last sensor values are persisted.
- `FAULT` can only be cleared by:
  - Specific button sequence (documented), **or**
  - Power cycle + successful sensor validation + explicit user confirmation via button.
- While in `FAULT`, the valve is forced closed on every wake and no watering is attempted.

**State machine states** (persisted):
- `NORMAL`
- `FAULT` (with reason enum)
- `CALIBRATE` (transient)
- `MANUAL_OVERRIDE` (times out back to NORMAL after N minutes for safety)

This structure makes it extremely difficult for the system to cause damage without multiple independent failures.

---

## §6. Persistent State — EEPROM Layout and Integrity Strategy

The ATmega328P has 1 KB of EEPROM with ~100 000 write/erase cycles per cell. We must treat it carefully.

**Proposed memory map** (addresses are examples; exact layout will be in firmware headers):

- `0x000–0x001`: Magic (0xC0DE) + structure version (1 byte) + overall CRC-16
- `0x002`: Current high-level state (NORMAL/FAULT/...)
- `0x003`: Last FAULT reason code (enum)
- Calibration block (16 bytes):
  - `dry_min`, `wet_max` (uint16)
  - `threshold_offset` (int8, in % of range)
  - `last_cal_timestamp` (uint32, RTC epoch)
  - Adaptive baseline wet value + slow EMA state
- Daily budget (8 bytes):
  - `water_seconds_or_pulses_today`
  - `last_water_day` (date from RTC)
  - Configured max per day
- Config block (32 bytes):
  - Pulse length (ms)
  - Absorption time (minutes)
  - Allowed irrigation hours bitmask or dawn/dusk flag
  - Low-batt threshold (mV)
  - No-response threshold, etc.
- Event log: 16 slots × 14 bytes = 224 bytes
  - Each slot: timestamp (4), reason/action (1), moisture (2), vbat (2), extra (5)
- CRC per major block + a master CRC.

**Integrity & atomicity strategy**:
- Never write a block without first computing a fresh CRC over its content.
- Use a simple "current slot + backup slot" scheme for the most critical values (state + budget) so a torn write leaves the backup intact.
- On boot, validate every block's CRC. If a block is bad, fall back to safe defaults and force `FAULT` with reason `EEPROM_CORRUPT`.
- For higher endurance (optional future): support an external I2C FRAM (MB85RC256V etc.) — 10^12 cycles, same interface.

Wear leveling: The event log is circular by design. Critical counters are updated only when they actually change (not every wake).

---

## §7. Calibration and Threshold Philosophy

**Problems with the original potentiometer approach** (for the Ultra target):
- Requires physical access and a meter or trial-and-error.
- Cannot support adaptive baseline or temperature compensation.
- Prevents the system from learning "what good wet soil actually looks like in *this* location and soil type."

**Chosen approach**:
1. **First-boot self-calibration**: On very first power-up (or after factory reset), the system enters `CALIBRATE` mode. User is guided (via LED blink codes or simple serial) to:
   - Place sensor in completely dry soil (or air) → record `dry_min`.
   - Thoroughly water the area and wait for absorption → record `wet_max`.
   - Or use a "known good wet" reference the user provides.
2. **Manual re-cal trigger**: A button combination at any time forces a new calibration cycle.
3. **Slow adaptive baseline**: While running in `NORMAL`, the system maintains a very slow exponential moving average of the "wet" readings observed after successful watering events. This slowly tracks seasonal drift, compaction, and sensor aging without user intervention.
4. **Threshold expressed as % of current range + offset** (e.g., "water when below 35 % of (wet_max – dry_min)").

The physical potentiometer is **deprecated for the Ultra target** but may be retained as a simple "manual offset knob" in the Smart/ESP32 version or as a beginner-friendly prototype path. The primary interface for production is self-cal + adaptive + buttons.

---

## §8. Actuation — Latching Valve Driver

**Primary driver IC**: DRV8833 (or pin-compatible). Low quiescent current, built-in dead time, thermal protection, and current limiting.

**Pulse parameters** (all configurable in EEPROM, with safe defaults):
- Open pulse: 40–80 ms (one polarity)
- Close pulse: 40–80 ms (reverse polarity)
- Dead time between polarity changes: 10–20 ms
- Maximum allowed pulse length (safety cap): 150 ms

**Bulk capacitor**: 1000 µF low-ESR electrolytic (or 2× 470 µF) placed immediately at the H-bridge VM pin. This supplies the high current pulse so the main battery and wiring see only a modest current spike.

**Safety close on total power loss (stretch goal)**: A large capacitor + simple RC + MOSFET path that can deliver one guaranteed close pulse even if the MCU is dead. This is a hardware-only feature documented in the PCB.

**Conventional NC solenoid fallback path**: Will be supported in the same driver code (just hold one polarity high for the desired duration). It will be clearly labeled "higher power, simpler valve, not recommended for battery/solar use."

---

## §9. Sensing — Analog Power-Gated + Optional STEMMA

Both sensor types are first-class citizens:

- **Analog capacitive** (Gikfun-style v1.2 / v2.0 blue boards, etc.): Cheapest, simplest, widely available. Requires the power-gating MOSFET + careful analogRead + oversampling.
- **Adafruit STEMMA Soil Sensor** (I2C, SeeSaw, capacitive + temperature): More expensive (~$10–15), but digital (noise immune over long cables), includes temperature for compensation, supports up to 4 addresses, and is far more consistent unit-to-unit. Preferred for serious or multi-zone installations.

**Runtime selection**: The firmware will probe for a STEMMA at the default address (0x36) at boot. If present and responding, it becomes the active sensor. Otherwise it falls back to the analog pin. This gives users a simple upgrade path.

**Sampling strategy** (common to both):
- Power rail on → wait 5–10 ms for stabilization → N samples → statistical cleaning → validate → power rail off.

---

## §10. Power Architecture and Energy Storage

**Preferred chemistry for Ultra target**: LiFePO4 (3.2 V nominal). Safer than Li-ion, longer cycle life, flatter discharge curve, can be run "direct" to a 3.3 V LDO or even directly to the MCU (within limits).

**Charging**: Small solar panel (5–6 V, 100–200 mA) + appropriate controller.
- For LiFePO4: CN3791 or similar MPPT IC is excellent.
- For Li-ion: TP4056 (with charging current reduced for small panels and careful layout).

**Multiple rails**:
- Always-on logic rail (3.3 V) for MCU + RTC VBAT path.
- Switched sensor rail (via P-MOS).
- Switched valve/H-bridge rail (via another P-MOS or load switch) — only powered when a pulse is imminent.

**Manual close hardware**: The PCB will include a clearly marked "manual close" test point or header that a user with a 9 V battery or charged capacitor can use to force the valve closed even if everything else is dead.

---

## §11. Future Extensibility and Open Questions

- Multi-zone expansion (shift registers or multiple H-bridges + one RTC).
- LoRa or nRF52 mesh sensor nodes feeding a central Ultra or Smart controller.
- FRAM instead of EEPROM for the event log.
- Full ESPHome component that re-uses the C++ safety logic.
- Integration with irrigation_unlimited or similar Home Assistant custom components for evapotranspiration-based scheduling.
- Formal verification or extensive property-based testing of the state machine (stretch).

**Open question**: Should the "simple pot + WDT + conventional valve" path be preserved as a heavily documented "v2.2 Quick Prototype" example, or should we declare the new architecture the only supported path?

---

## Appendix A — Key References and Data Sources

- ATmega328P datasheet (power consumption section)
- DS3231 datasheet (VBAT current, control register settings for alarm on battery)
- Cave Pearl Project / Ed Mallon writings on DS3231 low-power mods
- Multiple 2024–2025 Arduino forum threads on achieving sub-µA 328P sleep
- Plantwatery, b-parasite, and MOiST controller GitHub projects (latching valve + solar examples)
- Adafruit STEMMA Soil Sensor learn guide

All numbers and claims in this document must be traceable to these or direct measurement.

---

**End of REASONING.md v1.0**