# Soil Moisture Sensor and Valve Control using Arduino

This is an Arduino interrupt-driven program that monitors the moisture level of soil and opens a valve when the soil is too dry.

## How It Works
The program reads the moisture level using an analog moisture sensor connected to pin A0 and controls the valve using pin 13. The threshold for the moisture level is controlled by a potentiometer connected to interrupt pin 2. The program puts the board into low-power sleep mode in the loop function to minimize power consumption. When the moisture level falls below the threshold, the valve is opened to water the soil. The valve is closed again when the moisture level exceeds the threshold by 1%.

## Installation
To use this program, you will need an Arduino board and the following components:
- Moisture sensor
- Valve
- Potentiometer
- Breadboard
- Jumper wires

Connect the moisture sensor to pin A0, the valve to pin 13, and the potentiometer to interrupt pin 2. Make sure to also connect power and ground to each component.

## Usage
Compile and upload the program to your Arduino board using the Arduino IDE or another compatible IDE. The program will automatically start when the board is powered on. The program will continue to monitor the moisture level of the soil and open the valve when the soil is too dry.

## License
This program is released under the MIT License.
See the LICENSE file for more information.

## Credits
This program was developed by John Franks.
