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
 * which is attached to the internal comparator along with the moisture sensor.
 *
 * The program  puts the board into low-power sleep mode in the loop function
 * to minimize power consumption. When the moisture level falls below the
 * threshold set by the POT, the valve is opened to water the soil. The valve 
 * is closed again when the moisture level falls below the moisture sensor
 * value.
 *
 * The program uses the LowPower library for low-power sleep mode and
 * analogComp to manage the Arduino's comparator.
 * 
 * Note: The water valve that is attached must be a 'normally closed' valve.
 * I.E. With no power to the valve the valve is closed.
 */

#include <LowPower.h>
#include <analogComp.h>

const int moistureSensorPin = 6;  // Pin AIN0 connected to moisture sensor
const int potPin = 7;             // Pin AIN1 connected to POT (interrupt pin)
const int valvePin = 13;          // Pin connected to valve

// Function prototypes
void setup(void);                 // Arduino initialization function
void loop(void);                  // Arduino main loop
void moistureLowInterrupt(void);
void moistureHighInterrupt(void);

#ifdef DEBUG
volatile bool valveOpen = false;
#endif /* DEBUG */


/**
 * @brief Closes the water valve.
 *
 * This function closes the water valve by setting the valvePin to LOW.
 *
 * This function is declared as static inline, which suggests to the compiler
 * to insert the function's code directly into the place where it is called,
 * instead of performing a regular function call.
 *
 * @note This function does not check if the valve is already closed. Calling
 * this function when the valve is already closed will have no effect.
 *
 * @return void
 */
static inline void closeWaterValve(void)
{
  digitalWrite(valvePin, LOW); // Close valve.
#ifdef DEBUG
  valveOpen = false;
#endif /* DEBUG */
}


/**
 * @brief Opens the water valve.
 *
 * This function opens the water valve by setting the valvePin to HIGH.
 *
 * This function is declared as static inline to allow the compiler
 * to insert the function's code directly into the place where it is called,
 * instead of performing a regular function call.
 *
 * @note This function does not check if the valve is already opened. Calling
 * this function when the valve is already opened will have no effect.
 *
 * @return void
 */
static inline void openWaterValve(void)
{
  digitalWrite(valvePin, HIGH); // Open valve.
#ifdef DEBUG
  valveOpen = true;
#endif /* DEBUG */
}


/**
 * Sets up the initial state of the Arduino board and its peripherals.
 *
 * @param None.
 * @return None.
 *
 * This function initializes the various pins and peripherals used by the
 * program, including the moisture sensor pin, the valve pin, and the POT pin.
 * It also sets up the interrupt service routines for the analog comparator 
 * which measures the difference voltage differenct between the pot and
 * the moisture sensor output pins.
 *
 * Finally, the function enters a low-power mode by calling
 * LowPower.powerDown() to conserve power until the next comparator 
 * interrupt occurs.
 */

void setup(void)
{
#ifdef DEBUG
  // Initialize Serial communication for debugging
  Serial.begin(9600);

  // Wait for Serial connection to be established
  while (!Serial) {
    // Do nothing
  }

  // Print debug message to Serial monitor
  Serial.println("Program started");
#endif /* DEBUG */

  // Disable interrupts until all the subsystems are initialized.
  noInterrupts();

  // Initialize valve
  pinMode(valvePin, OUTPUT);
  digitalWrite(valvePin, LOW);     // Make sure valve is closed.

  analogComparator.setOn(AIN0, AIN1);    // Use voltages on both pins to compare
  analogComparator.enableInterrupt(moistureLowInterrupt, FALLING);
  
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
  LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);
#ifdef DEBUG
  if (valveOpen == true) {
    Serial.println("Valve OPEN");
  } else {
    Serial.println("Valve CLOSED");
  }
#endif /* DEBUG */
}


void moistureLowInterrupt(void)
{
  openWaterValve();
  analogComparator.enableInterrupt(moistureHighInterrupt, FALLING);
}

void moistureHighInterrupt(void)
{
  closeWaterValve();
  analogComparator.enableInterrupt(moistureLowInterrupt, RISING);
}
