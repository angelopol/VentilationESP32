# Ventilation Control System with ESP32

This project implements a system based on an ESP32 microcontroller that uses Bluetooth to receive commands and control a fan and LEDs based on the temperature and humidity measured by a DHT11 sensor.

## Main Features

### 1. Initial Setup (`setup`)
- Configures the PWM channel to control the fan.
- Sets the LED pins as outputs.
- Initializes serial and Bluetooth communication.
- Initializes the DHT11 sensor to measure temperature and humidity.

### 2. Receiving Commands via Bluetooth
- Uses the `BluetoothSerial` library to receive commands from a paired Bluetooth device.
- The received commands are stored in the `var` variable and processed to change the system's state.

### 3. LED Control
- The LEDs (red, green, and blue) indicate different system states:
  - **Blue LED**: Indicates that the system is measuring temperature and humidity.
  - **Red LED**: Indicates a resting state.
  - **Green LED**: Indicates that the fan is running.

### 4. Measuring Temperature and Humidity
- Uses the DHT11 sensor to measure:
  - **Relative Humidity (`h`)**
  - **Temperature (`t`)**
  - **Heat Index (`hic`)**, which combines temperature and humidity to calculate a thermal sensation.

### 5. Fan Control
- The fan is controlled via PWM (Pulse Width Modulation) based on the measured temperature and a reference value (`tmp`).
- The reference value is adjusted via Bluetooth commands (`a` to `s`), which correspond to different target temperatures.

### 6. System States
- The system has several states (`i`), controlled by Bluetooth commands (`0` to `7`):
  - **State 0**: Resting (fan off, red LED on).
  - **State 1-5**: Different fan speed levels, controlled by PWM.
  - **State 6**: Temperature-based control mode (turns on the fan if the temperature exceeds the reference value).
  - **State 7**: Similar to state 6, but with a different behavior in PWM calculation.

### 7. PWM Calculation
- In state 7, the PWM is dynamically adjusted based on the difference between the measured temperature and the reference value.

### 8. Main Loop (`loop`)
- The code enters specific `while` loops for each state (`i`).
- Within each loop:
  - Bluetooth commands are read to change the state or adjust the reference value.
  - The LEDs and fan PWM are updated according to the current state.

### 9. Bluetooth Interaction
- Sends temperature and humidity data to the connected Bluetooth device.
- Allows real-time adjustment of the system's behavior via commands.

## Summary
This project implements an intelligent ventilation control system that uses Bluetooth to receive commands, a DHT11 sensor to measure temperature and humidity, and LEDs to indicate the system's state. The fan is controlled via PWM, and its speed is adjusted based on the measured temperature or specific commands.