/*
============================================================
 AUTONOMOUS DELIVERY ROBO CAR
 ESP32 + 2x TB6612FNG + 2x Encoder N20 + 2x Normal N20
 HC-SR04 + MPU6050 + LED

 ESP32 Arduino Core 3.x
============================================================
*/

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Preferences.h>

// ============================================================
// WIFI
// ============================================================

const char* AP_SSID = "Name";
const char* AP_PASSWORD = "your password";

WebServer server(80);
Preferences preferences;

// ============================================================
// PINS
// ============================================================

// TB6612 #1 - encoder motors
#define ENC_LEFT_PWM  4
#define ENC_RIGHT_PWM 27

// TB6612 #2 - normal motors
#define NORMAL_LEFT_PWM  16
#define NORMAL_RIGHT_PWM 13

// Shared direction pins
#define LEFT_IN1      25
#define LEFT_IN2      26
#define RIGHT_IN1     32
#define RIGHT_IN2     33

// Shared standby
#define STBY_PIN      23

// Encoders
#define LEFT_ENC_A    18
#define LEFT_ENC_B    19
#define RIGHT_ENC_A   34
#define RIGHT_ENC_B   35

// MPU6050
#define SDA_PIN       21
#define SCL_PIN       22

// HC-SR04
#define TRIG_PIN      5
#define ECHO_PIN      14

// LED
#define LED_PIN       17

// ============================================================
// PWM
// ============================================================

int encoderPWM = 180;
int normalPWM = 180;

#define PWM_FREQ 20000
#define PWM_RESOLUTION 8

// Apply each motor-group PWM setting to both left and right driver channels.
void setEncoderDriverPWM(int duty) {
  duty = constrain(duty, 0, 255);
  ledcWrite(ENC_LEFT_PWM, duty);
  ledcWrite(ENC_RIGHT_PWM, duty);
}

void setNormalDriverPWM(int duty) {
  duty = constrain(duty, 0, 255);
  ledcWrite(NORMAL_LEFT_PWM, duty);
  ledcWrite(NORMAL_RIGHT_PWM, duty);
}

#define LEFT_MOTOR_INVERT false
#define RIGHT_MOTOR_INVERT false

// Change to -1 if encoder direction is reversed.
#define LEFT_ENCODER_SIGN 1
#define RIGHT_ENCODER_SIGN 1

// ============================================================
// ENCODER / WHEEL
// ============================================================

#define ENCODER_COUNTS_PER_WHEEL_REV 1800.0
#define WHEEL_DIAMETER_MM 43.0

#define WHEEL_CIRCUMFERENCE_MM \
  (3.14159265359 * WHEEL_DIAMETER_MM)

#define MM_PER_COUNT \
  (WHEEL_CIRCUMFERENCE_MM / ENCODER_COUNTS_PER_WHEEL_REV)

// ============================================================
// ENCODER VARIABLES
// ============================================================

volatile long leftEncoderCount = 0;
volatile long rightEncoderCount = 0;

volatile uint8_t leftEncoderState = 0;
volatile uint8_t rightEncoderState = 0;

// ============================================================
// ROUTE STORAGE
// ============================================================

#define MAX_ROUTE_COMMANDS 40
#define MAX_LOCATIONS 5
#define LOCATION_NAME_LEN 31

struct RouteCommand {
  char type;
  uint32_t value;
};

struct SavedRoute {
  char name[LOCATION_NAME_LEN + 1];
  uint8_t commandCount;
  RouteCommand commands[MAX_ROUTE_COMMANDS];
  uint32_t magic;
};

#define ROUTE_MAGIC 0x524F424F

SavedRoute currentRoute;

int currentRouteCount = 0;

// ============================================================
// TEACHING
// ============================================================

bool teachingMode = true;

bool manualMovementActive = false;

char manualMovementType = 0;

bool encoderSegmentActive = false;

long segmentStartLeft = 0;
long segmentStartRight = 0;

// ============================================================
// TURN
// ============================================================

int turnState = 0;

// 0 = no turn
// 1 = left
// 2 = right

bool autonomousTurn = false;

unsigned long turnStartTime = 0;

#define TURN_TIMEOUT 5000

#define TURN_TARGET 90.0

float gyroAngle = 0.0;

unsigned long lastGyroTime = 0;

#define GYRO_SIGN 1.0

// ============================================================
// AUTONOMOUS
// ============================================================

enum AutoState {
  AUTO_IDLE,
  AUTO_MOVING,
  AUTO_TURN,
  AUTO_WAIT_OBSTACLE,
  AUTO_DESTINATION,
  AUTO_RETURNING,
  AUTO_FINISHED
};

AutoState autoState = AUTO_IDLE;

int autoIndex = 0;

bool returningToBase = false;

uint32_t autoTargetMM = 0;

char autoMovementDirection = 0;

// ============================================================
// ULTRASONIC
// ============================================================

float obstacleDistance = 999.0;

#define OBSTACLE_DISTANCE_CM 25.0

unsigned long lastUltrasonicRead = 0;

#define ULTRASONIC_INTERVAL 100

// ============================================================
// DESTINATION
// ============================================================

unsigned long destinationStartTime = 0;

#define DESTINATION_WAIT 5000

// ============================================================
// LED
// ============================================================

bool ledBlinkActive = false;
bool ledBlinkState = false;

int ledBlinkCount = 0;

unsigned long ledTimer = 0;

// ============================================================
// STATUS
// ============================================================

String robotStatus = "READY";

// ============================================================
// MPU6050
// ============================================================

#define MPU6050_ADDR 0x68

float gyroZOffset = 0.0;

// ============================================================
// ENCODER ISR
// ============================================================

void IRAM_ATTR leftEncoderISR() {

  uint8_t a = digitalRead(LEFT_ENC_A);
  uint8_t b = digitalRead(LEFT_ENC_B);

  uint8_t newState = (a << 1) | b;

  uint8_t transition =
    (leftEncoderState << 2) | newState;

  switch (transition) {

    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
      leftEncoderCount++;
      break;

    case 0b0010:
    case 0b1011:
    case 0b1101:
    case 0b0100:
      leftEncoderCount--;
      break;
  }

  leftEncoderState = newState;
}

void IRAM_ATTR rightEncoderISR() {

  uint8_t a = digitalRead(RIGHT_ENC_A);
  uint8_t b = digitalRead(RIGHT_ENC_B);

  uint8_t newState = (a << 1) | b;

  uint8_t transition =
    (rightEncoderState << 2) | newState;

  switch (transition) {

    case 0b0001:
    case 0b0111:
    case 0b1110:
    case 0b1000:
      rightEncoderCount++;
      break;

    case 0b0010:
    case 0b1011:
    case 0b1101:
    case 0b0100:
      rightEncoderCount--;
      break;
  }

  rightEncoderState = newState;
}

// ============================================================
// ENCODER READ
// ============================================================

long getLeftEncoder() {

  noInterrupts();
  long value = leftEncoderCount;
  interrupts();

  return value * LEFT_ENCODER_SIGN;
}

long getRightEncoder() {

  noInterrupts();
  long value = rightEncoderCount;
  interrupts();

  return value * RIGHT_ENCODER_SIGN;
}

// ============================================================
// ENCODER SEGMENT
// ============================================================

void resetEncoderSegment() {

  segmentStartLeft = getLeftEncoder();
  segmentStartRight = getRightEncoder();

  encoderSegmentActive = true;
}

float getSegmentDistanceMM() {

  if (!encoderSegmentActive)
    return 0.0;

  long leftNow = getLeftEncoder();
  long rightNow = getRightEncoder();

  long leftDelta =
    abs(leftNow - segmentStartLeft);

  long rightDelta =
    abs(rightNow - segmentStartRight);

  float averageCounts =
    (leftDelta + rightDelta) / 2.0;

  return averageCounts * MM_PER_COUNT;
}

// ============================================================
// MOTOR DIRECTION
// ============================================================

void setLeftDirection(bool forward) {

  if (LEFT_MOTOR_INVERT)
    forward = !forward;

  if (forward) {

    digitalWrite(LEFT_IN1, HIGH);
    digitalWrite(LEFT_IN2, LOW);

  } else {

    digitalWrite(LEFT_IN1, LOW);
    digitalWrite(LEFT_IN2, HIGH);
  }
}

void setRightDirection(bool forward) {

  if (RIGHT_MOTOR_INVERT)
    forward = !forward;

  if (forward) {

    digitalWrite(RIGHT_IN1, HIGH);
    digitalWrite(RIGHT_IN2, LOW);

  } else {

    digitalWrite(RIGHT_IN1, LOW);
    digitalWrite(RIGHT_IN2, HIGH);
  }
}

// ============================================================
// DRIVER
// ============================================================

void enableDriver() {

  digitalWrite(STBY_PIN, HIGH);
}

// ============================================================
// STOP MOTORS
// ============================================================

void stopMotors() {

  setEncoderDriverPWM(0);
  setNormalDriverPWM(0);

  digitalWrite(LEFT_IN1, LOW);
  digitalWrite(LEFT_IN2, LOW);

  digitalWrite(RIGHT_IN1, LOW);
  digitalWrite(RIGHT_IN2, LOW);
}

// ============================================================
// FORWARD
// ============================================================

void moveForward() {

  enableDriver();

  setLeftDirection(true);
  setRightDirection(true);

  setEncoderDriverPWM(constrain(encoderPWM, 0, 255));

  setNormalDriverPWM(constrain(normalPWM, 0, 255));
}

// ============================================================
// BACKWARD
// ============================================================

void moveBackward() {

  enableDriver();

  setLeftDirection(false);
  setRightDirection(false);

  setEncoderDriverPWM(constrain(encoderPWM, 0, 255));

  setNormalDriverPWM(constrain(normalPWM, 0, 255));
}

// ============================================================
// ROUTE
// ============================================================

void clearCurrentRoute() {

  memset(
    &currentRoute,
    0,
    sizeof(currentRoute)
  );

  currentRoute.magic = ROUTE_MAGIC;

  currentRouteCount = 0;
}

void addCurrentCommand(
  char type,
  uint32_t value
) {

  if (currentRouteCount >= MAX_ROUTE_COMMANDS) {

    robotStatus = "ROUTE MEMORY FULL";

    return;
  }

  currentRoute.commands[currentRouteCount].type =
    type;

  currentRoute.commands[currentRouteCount].value =
    value;

  currentRouteCount++;

  currentRoute.commandCount =
    currentRouteCount;

  Serial.print("COMMAND SAVED: ");
  Serial.print(type);
  Serial.print(" ");
  Serial.println(value);
}

// ============================================================
// SAVE CURRENT F/B SEGMENT
// ============================================================

void saveCurrentMovementSegment() {

  if (!manualMovementActive)
    return;

  float distance =
    getSegmentDistanceMM();

  Serial.print("SEGMENT DISTANCE = ");
  Serial.print(distance);
  Serial.println(" mm");

  if (distance >= 10.0) {

    uint32_t distanceMM =
      (uint32_t)(distance + 0.5);

    addCurrentCommand(
      manualMovementType,
      distanceMM
    );
  }

  manualMovementActive = false;
  manualMovementType = 0;
  encoderSegmentActive = false;
}

// ============================================================
// MANUAL FORWARD
// ============================================================

void startManualForward() {

  if (!teachingMode)
    return;

  if (autoState != AUTO_IDLE)
    return;

  if (turnState != 0)
    return;

  if (manualMovementActive)
    saveCurrentMovementSegment();

  resetEncoderSegment();

  manualMovementType = 'F';
  manualMovementActive = true;

  robotStatus = "MOVING FORWARD";

  moveForward();
}

// ============================================================
// MANUAL BACKWARD
// ============================================================

void startManualBackward() {

  if (!teachingMode)
    return;

  if (autoState != AUTO_IDLE)
    return;

  if (turnState != 0)
    return;

  if (manualMovementActive)
    saveCurrentMovementSegment();

  resetEncoderSegment();

  manualMovementType = 'B';
  manualMovementActive = true;

  robotStatus = "MOVING BACKWARD";

  moveBackward();
}

// ============================================================
// MPU6050
// ============================================================

void writeMPURegister(
  uint8_t reg,
  uint8_t value
) {

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(reg);
  Wire.write(value);
  Wire.endTransmission();
}

void initMPU6050() {

  // Wake up
  writeMPURegister(0x6B, 0x00);

  // Gyroscope ±250 deg/s
  writeMPURegister(0x1B, 0x00);

  // Accelerometer ±2g
  writeMPURegister(0x1C, 0x00);

  delay(100);
}

int16_t readGyroZRaw() {

  Wire.beginTransmission(MPU6050_ADDR);
  Wire.write(0x47);
  Wire.endTransmission(false);

  Wire.requestFrom(
    MPU6050_ADDR,
    2,
    true
  );

  if (Wire.available() < 2)
    return 0;

  int16_t value =
    (Wire.read() << 8) |
    Wire.read();

  return value;
}

void calibrateGyro() {

  robotStatus = "GYRO CALIBRATION";

  float sum = 0;

  const int samples = 500;

  Serial.println(
    "Keep robot completely still..."
  );

  for (int i = 0; i < samples; i++) {

    sum += readGyroZRaw();

    delay(3);
  }

  gyroZOffset =
    sum / samples;

  Serial.print("GYRO OFFSET = ");
  Serial.println(gyroZOffset);

  robotStatus = "READY";
}

void resetGyroAngle() {

  gyroAngle = 0.0;

  lastGyroTime = micros();
}

void updateGyro() {

  unsigned long now = micros();

  float dt =
    (now - lastGyroTime)
    / 1000000.0;

  lastGyroTime = now;

  if (dt <= 0 || dt > 0.1)
    return;

  int16_t raw =
    readGyroZRaw();

  float gyroDPS =
    (raw - gyroZOffset) / 131.0;

  gyroAngle +=
    gyroDPS * dt * GYRO_SIGN;
}

// ============================================================
// LEFT TURN
// ============================================================

void startLeftTurn(bool autoTurn) {

  // ----------------------------------------------------------
  // MANUAL
  // ----------------------------------------------------------

  if (!autoTurn) {

    if (!teachingMode)
      return;

    if (autoState != AUTO_IDLE)
      return;

    // Save F/B before L
    if (manualMovementActive)
      saveCurrentMovementSegment();

    // SAVE L IMMEDIATELY
    if (currentRouteCount >= MAX_ROUTE_COMMANDS) {

      robotStatus = "ROUTE MEMORY FULL";

      return;
    }

    addCurrentCommand('L', 90);

    Serial.println(
      "MANUAL L COMMAND SAVED"
    );
  }

  // ----------------------------------------------------------
  // TURN
  // ----------------------------------------------------------

  stopMotors();

  // Encoder does not record turn distance
  encoderSegmentActive = false;

  resetGyroAngle();

  turnStartTime = millis();

  turnState = 1;

  autonomousTurn = autoTurn;

  if (autoTurn) {

    autoState = AUTO_TURN;

    robotStatus = "AUTO LEFT TURN";

  } else {

    robotStatus = "LEFT TURN";
  }
}

// ============================================================
// RIGHT TURN
// ============================================================

void startRightTurn(bool autoTurn) {

  // ----------------------------------------------------------
  // MANUAL
  // ----------------------------------------------------------

  if (!autoTurn) {

    if (!teachingMode)
      return;

    if (autoState != AUTO_IDLE)
      return;

    // Save F/B before R
    if (manualMovementActive)
      saveCurrentMovementSegment();

    // SAVE R IMMEDIATELY
    if (currentRouteCount >= MAX_ROUTE_COMMANDS) {

      robotStatus = "ROUTE MEMORY FULL";

      return;
    }

    addCurrentCommand('R', 90);

    Serial.println(
      "MANUAL R COMMAND SAVED"
    );
  }

  // ----------------------------------------------------------
  // TURN
  // ----------------------------------------------------------

  stopMotors();

  encoderSegmentActive = false;

  resetGyroAngle();

  turnStartTime = millis();

  turnState = 2;

  autonomousTurn = autoTurn;

  if (autoTurn) {

    autoState = AUTO_TURN;

    robotStatus = "AUTO RIGHT TURN";

  } else {

    robotStatus = "RIGHT TURN";
  }
}

// ============================================================
// NEXT AUTO COMMAND
// ============================================================

void startNextAutoCommand();

// ============================================================
// TURN UPDATE
// ============================================================

void updateTurn() {

  if (turnState == 0)
    return;

  updateGyro();

  float angle =
    abs(gyroAngle);

  bool wasAuto =
    autonomousTurn;

  // ----------------------------------------------------------
  // TURN COMPLETE
  // ----------------------------------------------------------

  if (angle >= TURN_TARGET) {

    stopMotors();

    turnState = 0;

    autonomousTurn = false;

    encoderSegmentActive = false;

    gyroAngle = 0.0;

    // --------------------------------------------------------
    // MANUAL
    // L/R already saved when button was pressed.
    // --------------------------------------------------------

    if (!wasAuto) {

      robotStatus = "TURN COMPLETE";

      Serial.println(
        "MANUAL TURN COMPLETE"
      );

      Serial.print(
        "CURRENT ROUTE COUNT = "
      );

      Serial.println(
        currentRouteCount
      );

      return;
    }

    // --------------------------------------------------------
    // AUTONOMOUS
    //
    // THIS IS THE IMPORTANT FIX.
    // No advanceAutoIndex() function is required.
    // --------------------------------------------------------

    if (returningToBase) {

      autoIndex--;

    } else {

      autoIndex++;
    }

    startNextAutoCommand();

    return;
  }

  // ----------------------------------------------------------
  // TIMEOUT
  // ----------------------------------------------------------

  if (millis() - turnStartTime >
      TURN_TIMEOUT) {

    stopMotors();

    turnState = 0;

    autonomousTurn = false;

    encoderSegmentActive = false;

    autoState = AUTO_IDLE;

    robotStatus = "TURN TIMEOUT";

    Serial.println(
      "TURN TIMEOUT"
    );

    return;
  }

  // ----------------------------------------------------------
  // TURN PWM
  // ----------------------------------------------------------

  int pwm;

  if (angle < 45)
    pwm = 150;

  else if (angle < 70)
    pwm = 120;

  else if (angle < 82)
    pwm = 95;

  else
    pwm = 65;

  enableDriver();

  if (turnState == 1) {

    // LEFT
    setLeftDirection(false);
    setRightDirection(true);

  } else {

    // RIGHT
    setLeftDirection(true);
    setRightDirection(false);
  }

  setEncoderDriverPWM(pwm);

  setNormalDriverPWM(pwm);
}

// ============================================================
// ULTRASONIC
// ============================================================

float readUltrasonic() {

  digitalWrite(
    TRIG_PIN,
    LOW
  );

  delayMicroseconds(2);

  digitalWrite(
    TRIG_PIN,
    HIGH
  );

  delayMicroseconds(10);

  digitalWrite(
    TRIG_PIN,
    LOW
  );

  unsigned long duration =
    pulseIn(
      ECHO_PIN,
      HIGH,
      30000
    );

  if (duration == 0)
    return 999.0;

  return duration * 0.0343 / 2.0;
}

void updateUltrasonic() {

  if (millis() -
      lastUltrasonicRead <
      ULTRASONIC_INTERVAL)
    return;

  lastUltrasonicRead =
    millis();

  obstacleDistance =
    readUltrasonic();
}

bool obstacleDetected() {

  return (
    obstacleDistance > 0 &&
    obstacleDistance <
    OBSTACLE_DISTANCE_CM
  );
}

// ============================================================
// LED
// ============================================================

void startLEDBlink(
  int numberOfBlinks
) {

  ledBlinkCount =
    numberOfBlinks * 2;

  ledBlinkState = false;

  ledBlinkActive = true;

  ledTimer = millis();

  digitalWrite(
    LED_PIN,
    LOW
  );
}

void updateLEDBlink() {

  if (!ledBlinkActive)
    return;

  if (millis() - ledTimer < 200)
    return;

  ledTimer = millis();

  ledBlinkState =
    !ledBlinkState;

  digitalWrite(
    LED_PIN,
    ledBlinkState
  );

  ledBlinkCount--;

  if (ledBlinkCount <= 0) {

    ledBlinkActive = false;

    digitalWrite(
      LED_PIN,
      LOW
    );
  }
}

// ============================================================
// AUTO MOVEMENT
// ============================================================

void beginAutoMovement(
  char direction,
  uint32_t distanceMM
) {

  autoMovementDirection =
    direction;

  autoTargetMM =
    distanceMM;

  resetEncoderSegment();

  if (direction == 'F') {

    moveForward();

    robotStatus =
      "AUTO FORWARD";

  } else {

    moveBackward();

    robotStatus =
      "AUTO BACKWARD";
  }

  if (returningToBase)
    autoState = AUTO_RETURNING;
  else
    autoState = AUTO_MOVING;
}

// ============================================================
// NEXT AUTO COMMAND
// ============================================================

void startNextAutoCommand() {

  // ----------------------------------------------------------
  // OUTBOUND FINISHED
  // ----------------------------------------------------------

  if (!returningToBase &&
      autoIndex >= currentRouteCount) {

    stopMotors();

    autoTargetMM = 0;

    encoderSegmentActive = false;

    autoState =
      AUTO_DESTINATION;

    destinationStartTime =
      millis();

    robotStatus =
      "DESTINATION REACHED";

    startLEDBlink(1);

    Serial.println(
      "DESTINATION REACHED"
    );

    return;
  }

  // ----------------------------------------------------------
  // RETURN FINISHED
  // ----------------------------------------------------------

  if (returningToBase &&
      autoIndex < 0) {

    stopMotors();

    autoTargetMM = 0;

    encoderSegmentActive = false;

    autoState =
      AUTO_FINISHED;

    robotStatus =
      "BASE REACHED";

    startLEDBlink(1);

    Serial.println(
      "BASE REACHED"
    );

    return;
  }

  // ----------------------------------------------------------
  // GET COMMAND
  // ----------------------------------------------------------

  RouteCommand cmd =
    currentRoute.commands[
      autoIndex
    ];

  Serial.print(
    "AUTO INDEX = "
  );

  Serial.print(
    autoIndex
  );

  Serial.print(
    " COMMAND = "
  );

  Serial.print(
    cmd.type
  );

  Serial.print(
    " "
  );

  Serial.println(
    cmd.value
  );

  // ----------------------------------------------------------
  // F
  // ----------------------------------------------------------

  if (cmd.type == 'F') {

    if (returningToBase)

      beginAutoMovement(
        'B',
        cmd.value
      );

    else

      beginAutoMovement(
        'F',
        cmd.value
      );

    return;
  }

  // ----------------------------------------------------------
  // B
  // ----------------------------------------------------------

  if (cmd.type == 'B') {

    if (returningToBase)

      beginAutoMovement(
        'F',
        cmd.value
      );

    else

      beginAutoMovement(
        'B',
        cmd.value
      );

    return;
  }

  // ----------------------------------------------------------
  // L
  // ----------------------------------------------------------

  if (cmd.type == 'L') {

    encoderSegmentActive = false;

    if (returningToBase)

      startRightTurn(true);

    else

      startLeftTurn(true);

    return;
  }

  // ----------------------------------------------------------
  // R
  // ----------------------------------------------------------

  if (cmd.type == 'R') {

    encoderSegmentActive = false;

    if (returningToBase)

      startLeftTurn(true);

    else

      startRightTurn(true);

    return;
  }
}

// ============================================================
// AUTO MOVEMENT UPDATE
// ============================================================

void updateAutoMovement() {

  if (autoState != AUTO_MOVING &&
      autoState != AUTO_RETURNING)
    return;

  if (autoTargetMM == 0)
    return;

  // ----------------------------------------------------------
  // OBSTACLE
  // ----------------------------------------------------------

  if (obstacleDetected()) {

    // Preserve progress made on this route command before stopping.
    // updateObstacleWait() will restart the encoder segment at zero,
    // so reduce the target by the distance already travelled.
    float travelledMM = getSegmentDistanceMM();
    if (travelledMM >= autoTargetMM) {
      autoTargetMM = 1;
    } else {
      autoTargetMM -= (uint32_t)travelledMM;
    }

    stopMotors();

    autoState =
      AUTO_WAIT_OBSTACLE;

    robotStatus =
      "OBSTACLE DETECTED";

    startLEDBlink(3);

    Serial.println(
      "OBSTACLE DETECTED"
    );

    return;
  }

  // ----------------------------------------------------------
  // DISTANCE
  // ----------------------------------------------------------

  float distance =
    getSegmentDistanceMM();

  if (distance >=
      autoTargetMM) {

    stopMotors();

    autoTargetMM = 0;

    encoderSegmentActive =
      false;

    Serial.print(
      "SEGMENT COMPLETE = "
    );

    Serial.print(
      distance
    );

    Serial.println(
      " mm"
    );

    // --------------------------------------------------------
    // MOVE TO NEXT COMMAND
    // --------------------------------------------------------

    if (returningToBase)

      autoIndex--;

    else

      autoIndex++;

    startNextAutoCommand();
  }
}

// ============================================================
// OBSTACLE WAIT
// ============================================================

void updateObstacleWait() {

  if (autoState !=
      AUTO_WAIT_OBSTACLE)
    return;

  stopMotors();

  if (obstacleDetected())
    return;

  Serial.println(
    "PATH CLEAR"
  );

  // Start a fresh encoder measurement for only the remaining distance.
  // autoTargetMM was reduced when the obstacle was first detected.
  resetEncoderSegment();

  if (autoMovementDirection == 'F')

    moveForward();

  else

    moveBackward();

  if (returningToBase)

    autoState =
      AUTO_RETURNING;

  else

    autoState =
      AUTO_MOVING;

  robotStatus =
    "PATH CLEAR - RESUMING";
}

// ============================================================
// DESTINATION
// ============================================================

void updateDestination() {

  if (autoState !=
      AUTO_DESTINATION)
    return;

  stopMotors();

  if (millis() -
      destinationStartTime <
      DESTINATION_WAIT)
    return;

  Serial.println(
    "DESTINATION WAIT COMPLETE"
  );

  // ----------------------------------------------------------
  // RETURN TO BASE
  // ----------------------------------------------------------

  returningToBase = true;

  autoIndex =
    currentRouteCount - 1;

  autoTargetMM = 0;

  encoderSegmentActive =
    false;

  autoState =
    AUTO_RETURNING;

  robotStatus =
    "RETURNING TO BASE";

  startNextAutoCommand();
}

// ============================================================
// PREFERENCES
// ============================================================

bool locationExists(
  int index
) {

  if (index < 0 ||
      index >= MAX_LOCATIONS)
    return false;

  char key[12];

  sprintf(
    key,
    "route%d",
    index
  );

  return preferences.isKey(key);
}

void saveLocation(
  int index,
  const char* name
) {

  if (index < 0 ||
      index >= MAX_LOCATIONS)
    return;

  SavedRoute route;

  memset(
    &route,
    0,
    sizeof(route)
  );

  strncpy(
    route.name,
    name,
    LOCATION_NAME_LEN
  );

  route.name[
    LOCATION_NAME_LEN
  ] = '\0';

  route.commandCount =
    currentRouteCount;

  route.magic =
    ROUTE_MAGIC;

  for (int i = 0;
       i < currentRouteCount;
       i++) {

    route.commands[i] =
      currentRoute.commands[i];
  }

  char key[12];

  sprintf(
    key,
    "route%d",
    index
  );

  preferences.putBytes(
    key,
    &route,
    sizeof(route)
  );
}

bool loadLocation(
  int index
) {

  if (index < 0 ||
      index >= MAX_LOCATIONS)
    return false;

  char key[12];

  sprintf(
    key,
    "route%d",
    index
  );

  if (!preferences.isKey(key))
    return false;

  SavedRoute route;

  size_t size =
    preferences.getBytes(
      key,
      &route,
      sizeof(route)
    );

  if (size != sizeof(route))
    return false;

  if (route.magic !=
      ROUTE_MAGIC)
    return false;

  currentRoute =
    route;

  currentRouteCount =
    route.commandCount;

  if (currentRouteCount >
      MAX_ROUTE_COMMANDS)

    currentRouteCount =
      MAX_ROUTE_COMMANDS;

  return true;
}

void deleteLocation(
  int index
) {

  if (index < 0 ||
      index >= MAX_LOCATIONS)
    return;

  char key[12];

  sprintf(
    key,
    "route%d",
    index
  );

  preferences.remove(key);
}

// ============================================================
// WEB PAGE
// ============================================================

String htmlPage() {

  return R"rawliteral(
<!DOCTYPE html>
<html>

<head>

<meta name="viewport"
content="width=device-width,initial-scale=1">

<title>Autonomous Delivery Robo Car</title>

<style>

body {
  margin:0;
  padding:0;
  font-family:Arial,sans-serif;
  background:#f2f4f7;
  color:#222;
}

.container {
  max-width:1200px;
  margin:auto;
  padding:20px;
}

h1 {
  text-align:center;
}

.grid {
  display:grid;
  grid-template-columns:1fr 1fr 1fr;
  gap:18px;
}

.card {
  background:white;
  border-radius:12px;
  padding:18px;
  box-shadow:0 2px 8px rgba(0,0,0,.12);
}

button {
  border:0;
  border-radius:8px;
  padding:14px 20px;
  margin:5px;
  font-size:16px;
  cursor:pointer;
}

.forward {
  background:#28a745;
  color:white;
}

.backward {
  background:#dc3545;
  color:white;
}

.turn {
  background:#007bff;
  color:white;
}

.stop {
  background:#111;
  color:white;
}

.normal {
  background:#ddd;
}

.value {
  font-size:20px;
  font-weight:bold;
  margin:8px 0;
}

.status {
  padding:12px;
  border-radius:8px;
  background:#eee;
  margin-top:10px;
}

.route {
  max-height:280px;
  overflow-y:auto;
}

.routeItem {
  padding:8px;
  margin:4px 0;
  background:#f1f1f1;
  border-radius:6px;
}

input {
  padding:10px;
  width:90%;
  margin:5px 0;
  border:1px solid #ccc;
  border-radius:6px;
}

.location {
  padding:10px;
  margin:5px 0;
  background:#f3f3f3;
  border-radius:8px;
}

.small {
  font-size:13px;
  color:#666;
}

@media(max-width:900px) {

.grid {
  grid-template-columns:1fr;
}

}

</style>

</head>

<body>

<div class="container">

<h1>Autonomous Delivery Robo Car</h1>

<div class="grid">

<div class="card">

<h2>Manual Control</h2>

<div style="text-align:center">

<div>

<button class="forward"
onclick="send('/forward')">
F
</button>

</div>

<div>

<button class="turn"
onclick="send('/left')">
L
</button>

<button class="stop"
onclick="send('/stop')">
STOP
</button>

<button class="turn"
onclick="send('/right')">
R
</button>

</div>

<div>

<button class="backward"
onclick="send('/backward')">
B
</button>

</div>

</div>

<h3>Encoder Motor PWM</h3>

<input type="range"
min="0"
max="255"
value="180"
id="encPWM"
oninput="setPWM()">

<div id="encPWMValue">180</div>

<h3>Normal Motor PWM</h3>

<input type="range"
min="0"
max="255"
value="180"
id="normalPWM"
oninput="setPWM()">

<div id="normalPWMValue">180</div>

<button class="normal"
onclick="send('/newroute')">
NEW ROUTE
</button>

</div>

<div class="card">

<h2>Sensor Data</h2>

<div class="value">
Left Encoder:
<span id="leftEnc">0</span>
</div>

<div class="value">
Right Encoder:
<span id="rightEnc">0</span>
</div>

<div class="value">
Distance:
<span id="distance">0</span> mm
</div>

<div class="value">
Obstacle:
<span id="obstacle">---</span> cm
</div>

<div class="value">
Gyro:
<span id="gyro">0</span>°
</div>

<div class="status">
Status:
<span id="status">READY</span>
</div>

</div>

<div class="card">

<h2>Current Route</h2>

<div class="route"
id="route">
No commands
</div>

<input id="locationName"
placeholder="Location name">

<button class="normal"
onclick="saveLocation()">
SAVE LOCATION
</button>

</div>

<div class="card">

<h2>Saved Locations</h2>

<div id="locations">
Loading...
</div>

</div>

<div class="card">

<h2>Autonomous</h2>

<button class="forward"
onclick="send('/startauto')">
START
</button>

<button class="stop"
onclick="send('/stop')">
STOP
</button>

<p class="small">
HC-SR04 obstacle detection is active
during autonomous replay.
</p>

</div>

</div>

</div>

<script>

function send(url) {

fetch(url)
.then(function() {

update();
loadLocations();

});

}

function saveLocation() {

let name =
document.getElementById(
'locationName'
).value;

if(name.trim() === '')
name = 'Location';

fetch(
'/save?name=' +
encodeURIComponent(name)
)
.then(function() {

update();
loadLocations();

});

}

function setPWM() {

let e =
document.getElementById(
'encPWM'
).value;

let n =
document.getElementById(
'normalPWM'
).value;

document.getElementById(
'encPWMValue'
).innerText = e;

document.getElementById(
'normalPWMValue'
).innerText = n;

fetch(
'/pwm?enc=' +
e +
'&normal=' +
n
);

}

function update() {

fetch('/data')
.then(function(r) {
return r.json();
})
.then(function(d) {

document.getElementById(
'leftEnc'
).innerText =
d.leftEncoder;

document.getElementById(
'rightEnc'
).innerText =
d.rightEncoder;

document.getElementById(
'distance'
).innerText =
d.distance.toFixed(1);

document.getElementById(
'obstacle'
).innerText =
d.obstacle.toFixed(1);

document.getElementById(
'gyro'
).innerText =
d.gyro.toFixed(1);

document.getElementById(
'status'
).innerText =
d.status;

let route =
document.getElementById(
'route'
);

if(d.route.length === 0) {

route.innerHTML =
'No commands';

} else {

let s = '';

d.route.forEach(
function(c,i) {

let unit =
(c.type === 'L' ||
 c.type === 'R')
? '°'
: ' mm';

s +=
'<div class="routeItem">' +
(i + 1) +
'. ' +
c.type +
' ' +
c.value +
unit +
'</div>';

});

route.innerHTML = s;

}

});

}

function loadLocations() {

fetch('/locations')
.then(function(r) {
return r.json();
})
.then(function(data) {

let div =
document.getElementById(
'locations'
);

div.innerHTML = '';

data.forEach(
function(item) {

if(!item.exists)
return;

let d =
document.createElement(
'div'
);

d.className =
'location';

d.innerHTML =
'<b>' +
item.name +
'</b>' +
'<br>' +

'<button onclick="loadLocation(' +
item.index +
')">SELECT</button>' +

'<button onclick="deleteLocation(' +
item.index +
')">DELETE</button>';

div.appendChild(d);

});

});

}

function loadLocation(index) {

fetch(
'/load?index=' +
index
)
.then(function() {

update();
loadLocations();

});

}

function deleteLocation(index) {

fetch(
'/delete?index=' +
index
)
.then(function() {

loadLocations();

});

}

setInterval(
update,
500
);

setInterval(
loadLocations,
2000
);

update();
loadLocations();

</script>

</body>

</html>
)rawliteral";
}

// ============================================================
// WEB HANDLERS
// ============================================================

void handleRoot() {

  server.send(
    200,
    "text/html",
    htmlPage()
  );
}

void handleForward() {

  startManualForward();

  server.send(
    200,
    "text/plain",
    "OK"
  );
}

void handleBackward() {

  startManualBackward();

  server.send(
    200,
    "text/plain",
    "OK"
  );
}

void handleLeft() {

  startLeftTurn(false);

  server.send(
    200,
    "text/plain",
    "OK"
  );
}

void handleRight() {

  startRightTurn(false);

  server.send(
    200,
    "text/plain",
    "OK"
  );
}

// ============================================================
// EMERGENCY STOP
// ============================================================

void emergencyStop() {

  // Save current manual F/B segment
  if (teachingMode &&
      manualMovementActive) {

    saveCurrentMovementSegment();
  }

  stopMotors();

  turnState = 0;

  autonomousTurn = false;

  autoState = AUTO_IDLE;

  autoTargetMM = 0;

  encoderSegmentActive = false;

  returningToBase = false;

  ledBlinkActive = false;

  digitalWrite(
    LED_PIN,
    LOW
  );

  robotStatus = "STOPPED";
}

void handleStop() {

  emergencyStop();

  server.send(
    200,
    "text/plain",
    "STOPPED"
  );
}

// ============================================================
// NEW ROUTE
// ============================================================

void handleNewRoute() {

  emergencyStop();

  clearCurrentRoute();

  teachingMode = true;

  robotStatus = "NEW ROUTE";

  Serial.println(
    "NEW ROUTE CREATED"
  );

  server.send(
    200,
    "text/plain",
    "NEW ROUTE"
  );
}

// ============================================================
// SAVE LOCATION
// ============================================================

void handleSave() {

  if (manualMovementActive)
    saveCurrentMovementSegment();

  if (currentRouteCount == 0) {

    robotStatus = "EMPTY ROUTE";

    server.send(
      400,
      "text/plain",
      "EMPTY ROUTE"
    );

    return;
  }

  String name =
    server.arg("name");

  name.trim();

  if (name.length() == 0)
    name = "Location";

  int slot = -1;

  for (int i = 0;
       i < MAX_LOCATIONS;
       i++) {

    if (!locationExists(i)) {

      slot = i;
      break;
    }
  }

  if (slot < 0)
    slot = 0;

  saveLocation(
    slot,
    name.c_str()
  );

  robotStatus =
    "LOCATION SAVED";

  server.send(
    200,
    "text/plain",
    "SAVED"
  );
}

// ============================================================
// LOAD
// ============================================================

void handleLoad() {

  int index =
    server.arg("index").toInt();

  if (!loadLocation(index)) {

    server.send(
      404,
      "text/plain",
      "NOT FOUND"
    );

    return;
  }

  teachingMode = true;

  autoState = AUTO_IDLE;

  returningToBase = false;

  robotStatus =
    "LOCATION LOADED";

  server.send(
    200,
    "text/plain",
    "LOADED"
  );
}

// ============================================================
// DELETE
// ============================================================

void handleDelete() {

  int index =
    server.arg("index").toInt();

  deleteLocation(index);

  robotStatus =
    "LOCATION DELETED";

  server.send(
    200,
    "text/plain",
    "DELETED"
  );
}

// ============================================================
// PWM
// ============================================================

void handlePWM() {

  if (server.hasArg("enc")) {

    encoderPWM =
      constrain(
        server.arg("enc").toInt(),
        0,
        255
      );
  }

  if (server.hasArg("normal")) {

    normalPWM =
      constrain(
        server.arg("normal").toInt(),
        0,
        255
      );
  }

  server.send(
    200,
    "text/plain",
    "PWM UPDATED"
  );
}

// ============================================================
// START AUTONOMOUS
// ============================================================

void handleStartAuto() {

  stopMotors();

  if (currentRouteCount <= 0) {

    robotStatus = "NO ROUTE";

    server.send(
      400,
      "text/plain",
      "NO ROUTE"
    );

    return;
  }

  teachingMode = false;

  returningToBase = false;

  autoIndex = 0;

  autoTargetMM = 0;

  encoderSegmentActive = false;

  obstacleDistance = 999.0;

  turnState = 0;

  autonomousTurn = false;

  autoState = AUTO_IDLE;

  robotStatus = "AUTO STARTED";

  Serial.println();
  Serial.println(
    "=============================="
  );
  Serial.println(
    " AUTONOMOUS START"
  );
  Serial.println(
    "=============================="
  );

  startNextAutoCommand();

  server.send(
    200,
    "text/plain",
    "AUTO STARTED"
  );
}

// ============================================================
// DATA
// ============================================================

void handleData() {

  String json = "{";

  json +=
    "\"leftEncoder\":" +
    String(getLeftEncoder()) +
    ",";

  json +=
    "\"rightEncoder\":" +
    String(getRightEncoder()) +
    ",";

  json +=
    "\"distance\":" +
    String(
      getSegmentDistanceMM(),
      1
    ) +
    ",";

  json +=
    "\"obstacle\":" +
    String(
      obstacleDistance,
      1
    ) +
    ",";

  json +=
    "\"gyro\":" +
    String(
      gyroAngle,
      1
    ) +
    ",";

  String safeStatus =
    robotStatus;

  safeStatus.replace(
    "\"",
    "'"
  );

  json +=
    "\"status\":\"" +
    safeStatus +
    "\",";

  json += "\"route\":[";

  for (int i = 0;
       i < currentRouteCount;
       i++) {

    if (i > 0)
      json += ",";

    json += "{";

    json +=
      "\"type\":\"";

    json +=
      currentRoute.commands[i].type;

    json += "\",";

    json +=
      "\"value\":";

    json +=
      currentRoute.commands[i].value;

    json += "}";
  }

  json += "]";

  json += "}";

  server.send(
    200,
    "application/json",
    json
  );
}

// ============================================================
// LOCATIONS
// ============================================================

void handleLocations() {

  String json = "[";

  for (int i = 0;
       i < MAX_LOCATIONS;
       i++) {

    if (i > 0)
      json += ",";

    json += "{";

    json +=
      "\"index\":" +
      String(i) +
      ",";

    bool exists =
      locationExists(i);

    json +=
      "\"exists\":" +
      String(
        exists ? "true" : "false"
      );

    if (exists) {

      char key[12];

      sprintf(
        key,
        "route%d",
        i
      );

      SavedRoute route;

      memset(
        &route,
        0,
        sizeof(route)
      );

      preferences.getBytes(
        key,
        &route,
        sizeof(route)
      );

      String name =
        String(route.name);

      name.replace(
        "\"",
        "'"
      );

      json +=
        ",\"name\":\"" +
        name +
        "\"";
    }

    json += "}";
  }

  json += "]";

  server.send(
    200,
    "application/json",
    json
  );
}

// ============================================================
// WEB SERVER
// ============================================================

void setupWebServer() {

  server.on(
    "/",
    HTTP_GET,
    handleRoot
  );

  server.on(
    "/forward",
    HTTP_GET,
    handleForward
  );

  server.on(
    "/backward",
    HTTP_GET,
    handleBackward
  );

  server.on(
    "/left",
    HTTP_GET,
    handleLeft
  );

  server.on(
    "/right",
    HTTP_GET,
    handleRight
  );

  server.on(
    "/stop",
    HTTP_GET,
    handleStop
  );

  server.on(
    "/newroute",
    HTTP_GET,
    handleNewRoute
  );

  server.on(
    "/save",
    HTTP_GET,
    handleSave
  );

  server.on(
    "/load",
    HTTP_GET,
    handleLoad
  );

  server.on(
    "/delete",
    HTTP_GET,
    handleDelete
  );

  server.on(
    "/pwm",
    HTTP_GET,
    handlePWM
  );

  server.on(
    "/startauto",
    HTTP_GET,
    handleStartAuto
  );

  server.on(
    "/data",
    HTTP_GET,
    handleData
  );

  server.on(
    "/locations",
    HTTP_GET,
    handleLocations
  );

  server.begin();

  Serial.println(
    "WEB SERVER STARTED"
  );
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  delay(1000);

  Serial.println();
  Serial.println(
    "=========================================="
  );
  Serial.println(
    " AUTONOMOUS DELIVERY ROBO CAR"
  );
  Serial.println(
    "=========================================="
  );

  // ----------------------------------------------------------
  // GPIO
  // ----------------------------------------------------------

  pinMode(
    LEFT_IN1,
    OUTPUT
  );

  pinMode(
    LEFT_IN2,
    OUTPUT
  );

  pinMode(
    RIGHT_IN1,
    OUTPUT
  );

  pinMode(
    RIGHT_IN2,
    OUTPUT
  );

  pinMode(
    STBY_PIN,
    OUTPUT
  );

  pinMode(
    LEFT_ENC_A,
    INPUT
  );

  pinMode(
    LEFT_ENC_B,
    INPUT
  );

  pinMode(
    RIGHT_ENC_A,
    INPUT
  );

  pinMode(
    RIGHT_ENC_B,
    INPUT
  );

  pinMode(
    TRIG_PIN,
    OUTPUT
  );

  pinMode(
    ECHO_PIN,
    INPUT
  );

  pinMode(
    LED_PIN,
    OUTPUT
  );

  digitalWrite(
    LED_PIN,
    LOW
  );

  digitalWrite(
    STBY_PIN,
    LOW
  );

  // ----------------------------------------------------------
  // PWM
  // ----------------------------------------------------------

  ledcAttach(ENC_LEFT_PWM, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(ENC_RIGHT_PWM, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(NORMAL_LEFT_PWM, PWM_FREQ, PWM_RESOLUTION);
  ledcAttach(NORMAL_RIGHT_PWM, PWM_FREQ, PWM_RESOLUTION);

  setEncoderDriverPWM(0);
  setNormalDriverPWM(0);

  // ----------------------------------------------------------
  // INITIAL ENCODER STATES
  // ----------------------------------------------------------

  uint8_t la =
    digitalRead(LEFT_ENC_A);

  uint8_t lb =
    digitalRead(LEFT_ENC_B);

  leftEncoderState =
    (la << 1) | lb;

  uint8_t ra =
    digitalRead(RIGHT_ENC_A);

  uint8_t rb =
    digitalRead(RIGHT_ENC_B);

  rightEncoderState =
    (ra << 1) | rb;

  // ----------------------------------------------------------
  // INTERRUPTS
  // ----------------------------------------------------------

  attachInterrupt(
    digitalPinToInterrupt(
      LEFT_ENC_A
    ),
    leftEncoderISR,
    CHANGE
  );

  attachInterrupt(
    digitalPinToInterrupt(
      LEFT_ENC_B
    ),
    leftEncoderISR,
    CHANGE
  );

  attachInterrupt(
    digitalPinToInterrupt(
      RIGHT_ENC_A
    ),
    rightEncoderISR,
    CHANGE
  );

  attachInterrupt(
    digitalPinToInterrupt(
      RIGHT_ENC_B
    ),
    rightEncoderISR,
    CHANGE
  );

  // ----------------------------------------------------------
  // I2C
  // ----------------------------------------------------------

  Wire.begin(
    SDA_PIN,
    SCL_PIN
  );

  Wire.setClock(400000);

  // ----------------------------------------------------------
  // MPU6050
  // ----------------------------------------------------------

  initMPU6050();

  delay(300);

  calibrateGyro();

  // ----------------------------------------------------------
  // NVS
  // ----------------------------------------------------------

  preferences.begin(
    "routes",
    false
  );

  clearCurrentRoute();

  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  WiFi.mode(WIFI_AP);

  WiFi.softAP(
    AP_SSID,
    AP_PASSWORD
  );

  Serial.println();

  Serial.print(
    "WiFi AP: "
  );

  Serial.println(
    AP_SSID
  );

  Serial.print(
    "IP address: "
  );

  Serial.println(
    WiFi.softAPIP()
  );

  // ----------------------------------------------------------
  // SERVER
  // ----------------------------------------------------------

  setupWebServer();

  robotStatus =
    "READY";

  Serial.println();
  Serial.println(
    "SYSTEM READY"
  );
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  server.handleClient();

  updateLEDBlink();

  // HC-SR04 is active only during autonomous mode.
  if (!teachingMode) {

    updateUltrasonic();
  }

  // Turn has priority.
  if (turnState != 0) {

    updateTurn();
  }

  // Autonomous movement.
  if (autoState == AUTO_MOVING ||
      autoState == AUTO_RETURNING) {

    updateAutoMovement();
  }

  // Obstacle waiting.
  if (autoState ==
      AUTO_WAIT_OBSTACLE) {

    updateObstacleWait();
  }

  // Destination waiting.
  if (autoState ==
      AUTO_DESTINATION) {

    updateDestination();
  }

  delay(2);
}