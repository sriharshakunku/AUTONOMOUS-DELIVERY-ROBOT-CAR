# Autonomous Delivery Robot Car

An ESP32-based robot car project that records a manually driven route, stores it, and replays it for delivery. The project combines encoder feedback, gyroscope-based turning, ultrasonic obstacle sensing, and an LED destination indicator.

> **Project status:** The project team reports that the functional checks are working. The test-results file currently has no recorded measurements or test dates. Add actual October 2026 readings to that file before presenting them as measured results. The four motor direction GPIOs are known, but their exact connections to the TB6612FNG input pins must be checked against the firmware and physical wiring before powering the motors.

## What the robot does

- Accepts manual movement commands using the control method already included in the ESP32 firmware.
- Uses encoder feedback to estimate wheel movement and record route distances.
- Uses the MPU6050 gyroscope to support left and right turns.
- Saves a taught route in ESP32 non-volatile storage (`Preferences`).
- Replays a saved route autonomously.
- Uses the HC-SR04 to detect an obstacle, stop, and wait. After the path clears, the firmware continues the remaining part of the current movement command.
- Blinks the destination LED once and waits for five seconds at the destination.
- Returns to base by reversing the saved route commands.

Exact controls, timing, thresholds, and firmware settings depend on the Arduino sketch included in the repository. Keep that sketch with the project and update this README if its behavior changes.

## System overview

The ESP32 is the main controller. It reads the encoders, MPU6050, and HC-SR04, then sends PWM, direction, and standby signals to two TB6612FNG motor-driver boards. One driver is assigned to the two encoder-equipped N20 motors; the other is assigned to the two standard N20 motors. The 12 V battery feeds separate buck converters for the motor and control rails.

![Robot operating flowchart](diagrams/robot-flowchart.svg)

![Power distribution diagram](diagrams/power-distribution.svg)

## Hardware

| Component | Quantity | Project use |
|---|---:|---|
| ESP32 DevKit with WROOM-32 | 1 | Main controller |
| TB6612FNG dual motor-driver boards | 2 | Motor PWM, direction, and standby control |
| Encoder-equipped N20 motors | 2 | Drive and wheel feedback |
| Standard N20 motors | 2 | Additional drive motors |
| 43 mm wheels | 4 | Robot motion |
| HC-SR04 ultrasonic sensor | 1 | Obstacle detection |
| MPU6050 module | 1 | Gyroscope and motion data |
| LED indicator | 1 | Destination indication |
| 12 V battery | 1 | Main power source |
| LM2596 buck converters | 2 | Separate 6 V motor and 5 V control/sensor rails |

See [`hardware/component-list.csv`](hardware/component-list.csv) for the component list and [`hardware/wiring-notes.md`](hardware/wiring-notes.md) for the connection sequence and checks.

## ESP32 pin map

GPIO numbers are used below. Check the labels on the specific DevKit and motor-driver boards before wiring.

| ESP32 pin | Connection | Purpose |
|---|---|---|
| GPIO4 | Encoder-motor driver `PWMA` | Left encoder-motor speed |
| GPIO27 | Encoder-motor driver `PWMB` | Right encoder-motor speed |
| GPIO16 | Standard-motor driver `PWMA` | Left standard-motor speed |
| GPIO13 | Standard-motor driver `PWMB` | Right standard-motor speed |
| GPIO25, GPIO26, GPIO32, GPIO33 | TB6612FNG direction input signals | Motor direction control; confirm each driver-input assignment in the firmware |
| GPIO23 | `STBY` on both TB6612FNG boards | Driver standby control |
| GPIO18 / GPIO19 | Left encoder A / B | Encoder feedback |
| GPIO34 / GPIO35 | Right encoder A / B | Encoder feedback; input-only GPIOs with no internal pull-ups |
| GPIO5 | HC-SR04 `TRIG` | Ultrasonic trigger; GPIO5 is a boot-strapping pin on the classic ESP32 |
| GPIO14 | HC-SR04 `ECHO` through a voltage divider | Ultrasonic echo input; keep the ESP32-side signal at 3.3 V or less |
| GPIO17 | LED indicator | Destination signal |
| GPIO21 / GPIO22 | MPU6050 `SDA` / `SCL` | I2C data and clock |

The complete map, including signal directions and electrical notes, is in [`hardware/esp32-pin-map.csv`](hardware/esp32-pin-map.csv).

> **Direction wiring is not fully documented:** GPIO25, GPIO26, GPIO32, and GPIO33 are the direction-control GPIOs, but the source information does not identify which GPIO connects to each `AIN1`, `AIN2`, `BIN1`, or `BIN2` pin on each board. Confirm this from the actual firmware and wiring. Do not guess the order or power the motor rail until the connections are confirmed.

## Power distribution and wiring

| Power connection | Destination | Notes |
|---|---|---|
| 12 V battery positive | Input positive on both LM2596 converters | Observe polarity. |
| 12 V battery negative | Input negative on both converters and common ground | Join all grounds together. |
| LM2596 motor output | `VM` on both TB6612FNG boards | Set to 6 V and verify with a multimeter first. Check that motor ratings and driver/converter current limits are suitable. |
| LM2596 control output | ESP32 board input labelled `5V` or `VIN` and compatible modules | Set to 5 V and verify before connection. Follow the label on the specific DevKit. |
| ESP32 `3V3` | `VCC` logic supply on both TB6612FNG boards | Keep separate from the 6 V motor supply. |
| Common ground | Battery negative, both converters, ESP32, drivers, sensors | A shared ground is required for the control signals. |

Important sensor and GPIO notes:

- Power the HC-SR04 from 5 V. Its `ECHO` output must pass through a voltage divider before GPIO14; the divider values are not specified in the project notes.
- Check the MPU6050 breakout-board label before powering it. Use 5 V only if that specific board supports it; otherwise use 3.3 V. Keep I2C pull-ups at 3.3 V.
- GPIO34 and GPIO35 are input-only and do not have internal pull-up or pull-down resistors. Check the encoder output type and provide external pull-ups if required. Encoder signals must not exceed 3.3 V.
- If using a bare LED, add a suitable series resistor. The resistor value is not specified here.
- Check that GPIO5 and its attached circuit allow the ESP32 to start reliably.
- Do not connect 12 V directly to the ESP32, sensors, or TB6612FNG supply pins.

## Repository contents

```text
.
├── README.md
├── .gitignore
├── docs/
│   └── setup-guide.md
├── diagrams/
│   ├── robot-flowchart.svg
│   └── power-distribution.svg
├── hardware/
│   ├── component-list.csv
│   ├── esp32-pin-map.csv
│   └── wiring-notes.md
├── tests/
│   ├── test-plan.md
│   └── test-results.csv
├── firmware/esp32/          # Add the existing Arduino sketch and its source files here
├── media/                   # Add actual project photos and demonstration videos here
├── Autonomous_Delivery_Robot_Car_Project_Report_Styled.docx
└── Autonomous_Delivery_Robot_Car_Presentation.pptx
```

The `firmware/esp32/` and `media/` folders are shown as the intended locations for the existing sketch and real project media. Add those files before publishing the repository. Do not upload private credentials; if the sketch contains Wi-Fi or other secrets, move them into a local-only configuration file and commit a safe example file instead.

## Firmware setup and upload

1. Place the existing ESP32 Arduino sketch and any supporting `.h` or `.cpp` files in `firmware/esp32/`.
2. Open the sketch in Arduino IDE and install ESP32 board support using Boards Manager.
3. Review the sketch's `#include` lines and install the libraries that the sketch actually uses. Do not add libraries that are not required by the code.
4. Review the configured GPIO values against [`hardware/esp32-pin-map.csv`](hardware/esp32-pin-map.csv) and the physical wiring.
5. Select the appropriate ESP32 board entry and connected serial port in Arduino IDE. Compile and upload the sketch.
6. Open Serial Monitor at the baud rate specified in the sketch, if the firmware uses serial output.
7. Keep all real passwords and credentials out of GitHub. Check `.gitignore` and inspect files before committing.

## Assembly and first power-on procedure

1. **Prepare the chassis.** Mount the four motors and 43 mm wheels. Identify left and right sides and label the encoder-equipped pair.
2. **Mount the electronics.** Secure the ESP32, both motor drivers, converters, battery, HC-SR04, MPU6050, and LED. Keep wiring clear of wheels and moving parts.
3. **Set the converters.** With the battery connected only to the converter inputs and downstream electronics disconnected, adjust one LM2596 to 6 V and the other to 5 V. Measure both outputs and check polarity.
4. **Wire power and ground.** Feed both converter inputs from the battery. Connect the 6 V output to both driver `VM` pins, the 5 V output to the ESP32 board's marked input and compatible modules, and ESP32 `3V3` to driver `VCC`. Join all grounds.
5. **Wire the sensors and indicator.** Follow the pin map. Add the HC-SR04 echo divider and any required encoder pull-ups or LED resistor. Confirm the MPU6050 board's supply and logic requirements.
6. **Confirm driver inputs.** Match GPIO25/26/32/33 to the actual TB6612FNG direction inputs by reading the firmware and checking the harness. Keep the motor supply disconnected until this is clear.
7. **Upload the firmware.** Upload the existing sketch and check that the ESP32 starts normally.
8. **Test with the wheels raised.** Enable one motor channel at a time at a low command. Verify left/right mapping, forward/reverse direction, stop, and standby behavior.
9. **Check feedback.** Confirm encoder counts respond to wheel movement, the MPU6050 responds to rotation, and the HC-SR04 reacts to a target.
10. **Run a short route.** Teach and save a short route, replay it in a clear area, then test obstacle waiting and return behavior under supervision.
11. **Record actual results.** Fill in the test-results CSV with dates and measurements from tests performed in October 2026. Do not use estimated values or the report cover/reference dates as test dates.

See [`docs/setup-guide.md`](docs/setup-guide.md) and [`hardware/wiring-notes.md`](hardware/wiring-notes.md) for the detailed setup and wiring checklists.

## Operating sequence

1. Power on the robot and confirm the controller and sensors start correctly.
2. Use the manual controls in the existing firmware to check movement and stop behavior.
3. Teach the desired route. Encoder feedback is used for distance commands and the MPU6050 for turns.
4. Save the route using the storage function in the firmware.
5. Start autonomous replay in a clear test area. The HC-SR04 obstacle logic should stop the robot and wait; after the obstacle clears, the current movement continues from its remaining distance.
6. At the destination, the LED blinks once and the robot waits for five seconds.
7. The robot then follows the reverse of the saved route to return to base.

Use the actual firmware's controls and settings. Do not operate the robot unattended or near people, stairs, traffic, or fragile objects.

## Test plan and results

The test plan covers these ten checks:

1. Power rails and startup
2. PWM motor-channel mapping
3. Motor direction and standby
4. Encoder feedback
5. HC-SR04 distance sensing
6. MPU6050 response
7. Route teaching and storage
8. Route replay
9. Obstacle waiting behavior
10. Destination indication and return

Use [`tests/test-plan.md`](tests/test-plan.md) for the procedure. Add results to [`tests/test-results.csv`](tests/test-results.csv) only when supported by actual observations. This log is scoped to tests performed from **2026-10-01 through 2026-10-31**. Include units with measurements and describe the test setup. No numerical results are currently recorded in the repository.

## Troubleshooting

| Symptom | Checks |
|---|---|
| ESP32 does not start | Verify the 5 V converter output and polarity, use the board's marked `5V`/`VIN` input, check common ground, and check whether GPIO5's attached circuit affects startup. |
| Driver powers up but motors do not move | Verify 6 V at `VM`, 3.3 V at `VCC`, common ground, `STBY` control, PWM signals, and the confirmed direction wiring. |
| A motor moves in the wrong direction | Disconnect motor power, then check motor lead polarity and the direction logic in the firmware. Re-test with the wheels raised. |
| Encoder readings stay at zero | Check encoder power and ground, A/B signal wiring, output voltage, and pull-up requirements. GPIO34/35 have no internal pull-ups. |
| HC-SR04 readings are missing or unstable | Check 5 V supply, `TRIG`, `ECHO` divider wiring, common ground, and target position. |
| MPU6050 is not detected or readings do not change | Verify its breakout supply rating, common ground, SDA/SCL wiring, and 3.3 V I2C logic. |
| Route replay differs between runs | Check battery condition, wheel traction, encoder connections, turn response, route settings, and test surface. Record the conditions for each run. |

## Project documents

- [Setup guide](docs/setup-guide.md)
- [Wiring notes](hardware/wiring-notes.md)
- [ESP32 pin map](hardware/esp32-pin-map.csv)
- [Component list](hardware/component-list.csv)
- [Robot flowchart](diagrams/robot-flowchart.svg)
- [Power distribution diagram](diagrams/power-distribution.svg)
- [Project report](Autonomous_Delivery_Robot_Car_Project_Report_Styled.docx)
- [Project presentation](Autonomous_Delivery_Robot_Car_Presentation.pptx)

## Technical references

- [Espressif ESP32 Series Datasheet](https://documentation.espressif.com/esp32_datasheet_en.html) — GPIO restrictions and boot configuration.
- [Espressif ESP32 DevKitC-1 board documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/boards/ESP32-DevKitC-1.html) — board pin information. Check the marking and revision of the specific DevKit used in this project.
- [Toshiba TB6612FNG datasheet](https://toshiba-semicon-storage.com/info/datasheet_en_20141001.pdf?did=10660) — driver pin names and electrical limits. Check the actual breakout board as well as the IC datasheet.
