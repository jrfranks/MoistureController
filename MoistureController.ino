/*
  MoistureController.ino - Interrupt-driven soil moisture controller with low-power sleep.
  Reads soil moisture on A0; if below threshold (set via pot on A1), opens valve on D13 for 10s.
  Hysteresis prevents chattering. Sleeps ~5 min between checks using WDT interrupts.

  Hardware:
  - Soil moisture sensor: Analog out to A0, VCC to 5V, GND to GND.
  - Solenoid valve: Signal to D13, VCC to 5V (or external relay if high current), GND to GND.
  - Potentiometer (10k): One end to 5V, other to GND, wiper to A1 (for threshold 200-800).
  - Optional: Button on D2 for future manual wake (not implemented).

  Calibration: Dry soil ~0-300, wet ~700-1023. Adjust map() in readThreshold() if needed.

  Bugs Fixed:
  - Threshold pin to A1 for proper analog read.
  - WDT interrupt mode with counter for ~5 min sleeps (no resets).
  - Corrected open/closeValve functions.
  - Always read threshold and check moisture on wake.

  License: MIT
*/

#include <avr/sleep.h>
#include <avr/power.h>
#include <avr/wdt.h>

#define MOISTURE_PIN A0
#define THRESHOLD_PIN A1    // Changed from 2 to A1 for analog read
#define VALVE_PIN 13

volatile bool wakeUp = false;
volatile int wdtCount = 0;
const int HYSTERESIS = 10;  // ~1% offset for anti-chatter
const int WATER_DURATION = 10000;  // 10 seconds
const int MAX_WAIT_TIME = 60000;   // Safety timeout for hysteresis wait (60s)
const long TARGET_SLEEP_MS = 300000;  // 5 min target, approximated with 8s WDT ticks

void setup() {
  Serial.begin(9600);
  pinMode(VALVE_PIN, OUTPUT);
  digitalWrite(VALVE_PIN, LOW);
  
  // Setup WDT for interrupt-only, 8s intervals
  setupWDT();
  
  Serial.println("Moisture Controller Started");
  Serial.println("Adjust pot on A1 for threshold (200-800)");
}

void loop() {
  if (wakeUp) {
    wakeUp = false;
    Serial.println("Woken from sleep - checking moisture...");
    
    int threshold = readThreshold();
    int moisture = analogRead(MOISTURE_PIN);
    
    Serial.print("Moisture: "); Serial.print(moisture);
    Serial.print(" | Threshold: "); Serial.println(threshold);
    
    if (moisture < threshold) {
      Serial.println("Soil dry - opening valve");
      openValve();
      delay(WATER_DURATION);
      closeValve();
      
      // Wait for moisture to rise (with hysteresis and timeout)
      unsigned long startWait = millis();
      while (analogRead(MOISTURE_PIN) < (threshold + HYSTERESIS) && (millis() - startWait < MAX_WAIT_TIME)) {
        delay(1000);
        Serial.print("Waiting for wet... Current: "); Serial.println(analogRead(MOISTURE_PIN));
      }
      if (millis() - startWait >= MAX_WAIT_TIME) {
        Serial.println("Hysteresis timeout - forcing close");
      }
    } else {
      Serial.println("Soil wet - no action");
    }
  }
  
  Serial.println("Entering sleep...");
  enterSleep();
}

int readThreshold() {
  int raw = analogRead(THRESHOLD_PIN);
  // Map 0-1023 to 200-800 (dry threshold low, wet high)
  return map(raw, 0, 1023, 200, 800);
}

void openValve() {
  digitalWrite(VALVE_PIN, HIGH);
  Serial.println("Valve OPEN");
}

void closeValve() {
  digitalWrite(VALVE_PIN, LOW);
  Serial.println("Valve CLOSED");
}

void enterSleep() {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  sleep_enable();
  
  power_all_disable();  // Shut down modules for power save
  
  sleep_mode();  // Sleep here until interrupt
  
  // Woken by WDT ISR
  sleep_disable();
  power_all_enable();
}

ISR(WDT_vect) {
  wdtCount++;
  // 8s per tick; 38 ticks ≈ 304s (close to 5 min)
  if (wdtCount >= 38) {
    wakeUp = true;
    wdtCount = 0;
  }
}

void setupWDT() {
  wdt_reset();
  cli();  // Disable interrupts during config
  wdt_disable();  // Stop current WDT
  
  // Unlock and set to interrupt-only mode
  WDTCSR |= (1 << WDCE) | (1 << WDE);
  // WDIE: enable interrupt; WDP3 + WDP0 = 8s prescaler
  WDTCSR = (1 << WDIE) | (1 << WDP3) | (1 << WDP0);
  
  sei();  // Re-enable interrupts
}
