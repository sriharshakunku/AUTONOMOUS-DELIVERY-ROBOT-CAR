# Autonomous Delivery Robot Car — Test Plan

Use this plan to check the robot after wiring and firmware setup. The expected observations below describe what to look for; they are not test results. Enter actual readings, dates, and pass/fail decisions in [`test-results.csv`](test-results.csv) only after each test is performed.

## October 2026 results scope

Keep this results log to tests actually performed from **2026-10-01 through 2026-10-31**. The project report shows an October 2026 cover date and October 2026 reference-access dates, but its test-results table contains `[enter]` placeholders only. Those dates are not test dates, and no measured results were present in the project files. The CSV therefore contains only its header until actual October test records are available. Do not enter estimates or treat report/reference dates as measurements.

## Before testing

- Confirm the TB6612FNG direction-pin mapping against the firmware and the physical wiring.
- Check the 5 V and 6 V power rails with a multimeter before connecting the ESP32, sensors, or motor drivers.
- Verify common ground and the HC-SR04 ECHO voltage divider.
- Raise the wheels for motor tests and keep hands clear of moving parts.
- Record the surface, battery state, firmware version, and any settings that could affect the results.
- Agree on any numerical tolerance before a test that needs one. The project notes do not specify accuracy thresholds.

## Test cases

| Test ID | Test | Setup and procedure | Expected observation / pass check |
|---|---|---|---|
| T1 | Power rails and startup | With the battery disconnected from the robot electronics, adjust and measure the two LM2596 outputs. Then connect the electronics and power up the ESP32. Record both rail readings and startup behavior. | The motor rail measures the configured 6 V and the controller rail measures the configured 5 V before connection. The ESP32 starts without visible wiring or power faults. Record actual readings; do not assume them. |
| T2 | PWM motor-channel mapping | Raise the wheels. Command one PWM channel at a time: encoder-driver left/right, then standard-driver left/right. Record the GPIO/channel and which physical motor responds. | Each command drives the intended motor channel, and the stop command stops it. No unexpected motor responds. |
| T3 | Motor direction and standby | With direction connections confirmed from the firmware, test each motor channel at low command, then stop it. Check the direction for forward and reverse commands and verify STBY behavior. | Each channel turns in the intended physical direction and stops when commanded. Record the actual GPIO-to-driver input mapping. |
| T4 | Encoder feedback | Keep the robot raised. Rotate or drive each encoder-equipped wheel separately. Record encoder counts for a repeatable movement and check the counts while stationary. | Counts change during wheel movement and remain unchanged while stationary. Record actual counts and the movement used; no target count is assumed. |
| T5 | HC-SR04 distance sensing | Place a flat target at measured distances. Record the reference distance and sensor reading at each position. Confirm the ECHO divider is connected before powering. | Readings respond to target distance and are plausible against the reference measurements. Use a tolerance agreed before testing and record it. |
| T6 | MPU6050 response | Keep the robot still, note the sensor output, then rotate the robot through known orientations by hand. Record the observed readings and setup. | Readings are available while still and change when the robot is rotated. Record actual values; no drift or accuracy limit is assumed. |
| T7 | Route teaching and storage | Use the project's manual controls to teach a short route. Save it using the implemented firmware procedure, restart if applicable, and check whether the route remains available. | The route can be recorded and retrieved according to the implemented firmware. Record the route and storage method used. |
| T8 | Route replay | Replay the route from T7 under the same floor and starting conditions. Record the endpoint and any deviation or interruption. | The robot follows the stored route as implemented. Compare the endpoint with a tolerance agreed before testing. |
| T9 | Obstacle waiting behavior | Run a short route in a clear area. Place an obstacle safely in front of the sensor and record whether the robot stops or waits. Remove the obstacle and record whether it resumes automatically or needs another command. | The observed behavior matches the behavior implemented in the firmware. Record the stop distance, wait behavior, and resume behavior; do not assume automatic resume. |
| T10 | Destination indication and return | Run a route that reaches its configured destination. Observe the LED, then observe the return-to-base behavior if implemented and enabled. Record destination and return observations separately. | The LED and return behavior match the current firmware configuration. Record actual results; if a behavior is not implemented or enabled, note that instead of marking it as passed. |

## Recording results

- Fill in `Test date (YYYY-MM-DD)`, `Setup`, `Measured result`, `Pass / fail`, and `Notes` in `test-results.csv` after testing. Add only actual October 2026 test records to this log.
- Include units with numeric measurements, such as `6.02 V`, `142 counts`, or `18 cm`.
- Use `Not tested` or `Not applicable` when appropriate. Do not enter estimated readings as measured values.
- Keep a photo or short video of important physical test setups in the project's media folder and note its filename in the results log.
- If a test fails, record the observed behavior and conditions so the issue can be repeated.
