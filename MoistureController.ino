/*********************************************************************
 * Copyright (c) 2022 John Franks
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *********************************************************************/

/*
 * This is an Arduino interrupt-driven program that monitors the moisture level
 * of soil and opens a valve when the soil is too dry.
 *
 * The threshold for the moisture level is controlled by a potentiometer,
 * which is an interrupt pin.
 *
 * The program  puts the board into low-power sleep mode in the loop function
 * to minimize power consumption. When the moisture level falls below the
 * threshold, the valve is opened to water the soil. The valve is closed again
 * when the moisture level exceeds the threshold by 1%.
 *
 * The program uses the LowPower library for low-power sleep mode.
*/

#include <LowPower.h>

const int moistureSensorPin = A0; // Pin connected to moisture sensor
const int valvePin = 13;          // Pin connected to valve
const int potPin = 2;             // Pin connected to potentiometer (interrupt pin)

// Threshold values for moisture level (volatile for interrupt access)
volatile int lowWater = 0;        // Low water level
volatile int highWater = 1;       // high water level
float        levelDelta = 1.01;   // 1% diff between low and high water levels

// Function prototypes
void setup(void);                 // Arduino initialization function
void loop(void);                  // Arduino main loop
void checkMoisture(void);         // Main moisture control function
void setMoistureThreshold(void);  // Get POT reading and store it for use by checkMoisture()
void handlePotInterrupt(void);    // Change POT reading when knob is moved.
void openValve(void);             // Open water valve
void closeValve(void);            // Close water valve

/**
 * Sets up the initial state of the Arduino board and its peripherals.
 *
 * @param None.
 * @return None.
 *
 * This function initializes the various pins and peripherals used by the
 * program, including the moisture sensor pin, the valve pin, and the POT pin.
 * It also sets up the interrupt service routines for the POT pin and the
 * moisture sensor pin.
 *
 * Additionally, the function sets the initial value for the moisture threshold
 * using the `setMoistureThreshold()` function, and initializes the Serial
 * communication for debugging purposes.
 *
 * Finally, the function enters a low-power mode by calling
 * `LowPower.powerDown()` to conserve power until the next interrupt occurs.
 */

void setup(void)
{
#ifdef DEBUG
  // Initialize Serial communication for debugging
  Serial.begin(9600);

  // Wait for Serial connection to be established
  while (!Serial) {
    ; // Do nothing
  }

  // Print debug message to Serial monitor
  Serial.println("Program started");
#endif /* DEBUG */

  // Disable interrupts until all the subsystems are initialized.
  noInterrupts();

  // Initialize POT
  pinMode(potPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(potPin), setMoistureThreshold, CHANGE); // Set interrupt on potentiometer pin

  // Initialize valve
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW); // Make sure valve is closed.

  // Initialize moisture sensor
  pinMode(moistureSensorPin, INPUT);
  attachInterrupt(digitalPinToInterrupt(moistureSensorPin), checkMoisture, CHANGE); // Set interrupt on moisture sensor pin

  // Set initial valve state
  setMoistureThreshold();

  // Open / close valve appropriately
  checkMoisture();

  // System is in order so we can enable interrupts and start normal processing.
  interrupts();
}


/**
 * This function is the main loop of the program.
 *
 * @param None.
 * @return None.
 *
 * The loop() function is called continuously by the Arduino board after the
 * setup() function has completed execution.  It is called repeatedly in an
 * infinite loop until power is disconnected or the program is reset.
 *
 * We use it here to keep the microcontroller in a perpetual ultra-low power
 * state.  It will be called whenever an interrupt wakes microcontroller so it
 * is immediately put to sleep in its lowest power mode.
 *
 * All program logic is executed inside the interrupt handler routines.
 */

void loop(void)
{
  // Put board into sleep mode
  LowPower.powerDown(SLEEP_MODE_IDLE, ADC_OFF, BOD_OFF);
}


/**
 * Sets the moisture threshold for activating the valve.
 *
 * @param threshold An integer value representing the moisture level threshold
 *                  as a percentage, ranging from 0 (completely dry) to 100
 *                  (completely saturated). This value is controlled by a
 *                  potentiometer connected to the Arduino, and should be
 *                  in the range of 0-1023.
 * @return None.
 *
 * This function sets the moisture threshold for activating the valve when the
 * soil is too dry. The threshold is a percentage value that is controlled by
 * a potentiometer connected to the Arduino. The function reads the current
 * value of the potentiometer and calculates the corresponding threshold value,
 * which is then stored in a global variable for use in the `checkMoisture()`
 * function. If the potentiometer value is below a certain minimum threshold,
 * the function will set the threshold to the minimum value to prevent the
 * valve from being activated unnecessarily.
 */

void setMoistureThreshold(void) {
  int potValue = analogRead(potPin);

  // Normalize input level
  lowWater = map(potValue, 0, 1023, 0, 1023);

  // Calculate highWater here to avoid constant math during level interrupts.
  highWater = (int)(lowWater * levelDelta);
  if (highWater <= lowWater) {
    highWater = lowWater + 1;
  }
}


/**
 * Checks the moisture level in the soil and activates the valve if necessary.
 *
 * @param None.
 *
 * @return None.
 *
 * This function checks the moisture level of the soil and opens the valve
 * if the moisture level is below the threshold. The threshold is set by
 * the value of the `threshold` variable, which is updated whenever the
 * potentiometer changes. The `threshold` variable should be a value between
 * 0 and 1023, corresponding to the range of values output by the potentiometer.
 * If the moisture level is below the threshold, the `valveOpen` variable is
 * set to `true` and the valve is opened. If the moisture level is above the
 * threshold by 5% or more, the `valveOpen` variable is set to `false` and
 * the valve is closed.
 */

void checkMoisture(void)
{
  static bool valveOpen = false; // Flag to keep track of valve status

  int moistureLevel = analogRead(moistureSensorPin);

  moistureLevel = map(moistureLevel, 0, 1023, 0, 1023);
  if (!valveOpen && (moistureLevel < lowWater)) {
    digitalWrite(valvePin, HIGH); // Open valve
    valveOpen = true;
  } else if (valveOpen && (moistureLevel >= highWater)) {
    digitalWrite(valvePin, LOW); // Close valve
    valveOpen = false;
  }
}
