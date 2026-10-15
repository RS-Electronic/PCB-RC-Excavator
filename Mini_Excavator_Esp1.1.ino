// ESP1_1-24_Gamepad_Master.txt
// ESP32 #1 (Classic DevKit) - Gamepad Master für Bagger 1:24
// Steuert: Pivot, MainBoom, Thumb, AuxAttach direkt über DRV8833
// Servos: clawServo, auxServo
// Licht: cabLights, auxLights
// Tracks, Dipper, TiltAttach werden per Serial an ESP32-S3 (Motor-Slave) gesendet.
//
// Tastenbelegung PS4 (Bluepad32-Mapping):
//   Kreuz   = ctl->a()  -> X-Doppelklick = Moduswechsel
//   Kreis   = ctl->b()  -> frei
//   Quadrat = ctl->x()  -> Thumb vorwärts
//   Dreieck = ctl->y()  -> Thumb rückwärts
//   L1/R1   -> Tracks vorwärts (Vollgas)
//   L2/R2   -> Schub-Schild
//   D-Pad   -> clawServo (up/down), auxServo (left/right)

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Bluepad32.h>

ControllerPtr myControllers[BP32_MAX_GAMEPADS];

// --- Pins ESP1 ---
#define clawServoPin 23
#define auxServoPin  22
#define cabLights    32
#define auxLights    33

// DRV8833 an ESP1 (Arm + Anbau)
#define pivot0       13  // Pivot DRV1-A IN1 Oberbau drehen; ctl->axisX() left
#define pivot1       12  // Pivot DRV1-A IN2 Oberbau drehen; ctl->axisX() right
#define mainBoom0    16  // MainBoom DRV1-B IN1 Hauptausleger; ctl->axisRY() down
#define mainBoom1    17  // MainBoom DRV1-B IN2 Hauptausleger; ctl->axisRY() up

#define thumb0       4   // Thumb DRV2-A IN1 Baggerdaumen, BUTTON_X (Quadrat)
#define thumb1       2   // Thumb DRV2-A IN2 Baggerdaumen, BUTTON_Y (Dreieck)
#define auxAttach0   25  // AuxAttach DRV2-B IN1 Schub-Schild, BUTTON_SHOULDER_L2
#define auxAttach1   26  // AuxAttach DRV2-B IN2 Schub-Schild, BUTTON_SHOULDER_R2

// --- PWM-Kanäle (LEDC) - Kanäle 8-15 ---
#define PWM_FREQ     20000
#define PWM_RES      8
#define CH_PIVOT0    8
#define CH_PIVOT1    9
#define CH_BOOM0     10
#define CH_BOOM1     11
#define CH_THUMB0    12
#define CH_THUMB1    13
#define CH_AUXA0     14
#define CH_AUXA1     15

#define ANALOG_DEADZONE 30
#define AXIS_MAX 512

// D-Pad Bitmasken (Bluepad32)
#define DPAD_UP    0x01
#define DPAD_DOWN  0x02
#define DPAD_RIGHT 0x04
#define DPAD_LEFT  0x08

// Debug (1 = an, 0 = aus)
#define DEBUG_AXES 1

// Doppelklick-Fenster
#define DOUBLE_CLICK_MS 1000

Servo clawServo;
Servo auxServo;

int clawServoValue = 90;
int auxServoValue = 90;
int servoDelay = 0;
int lightSwitchTime = 0;

bool cabLightsOn = false;
bool auxLightsOn = false;
bool moveClawServoUp = false;
bool moveClawServoDown = false;
bool moveAuxServoUp = false;
bool moveAuxServoDown = false;

// --- Doppelklick / Modus ---
bool joyTrackMode = false;
bool lastModeBtn = false;
uint32_t lastModePressTime = 0;

// --- Änderungs-Erkennung für Serial-Kommandos ---
int lastDipperCmd    = -1;
int lastDipperSpeed  = -1;
int lastTiltCmd      = -1;
int lastTiltSpeed    = -1;
int lastTrackLCmd    = -1;
int lastTrackLSpeed  = -1;
int lastTrackRCmd    = -1;
int lastTrackRSpeed  = -1;

void onConnectedController(ControllerPtr ctl) {
  bool foundEmptySlot = false;
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == nullptr) {
      Serial.printf("\nCALLBACK: Controller connected index=%d\n", i);
      ControllerProperties properties = ctl->getProperties();
      Serial.printf("Controller model: %s, VID=0x%04x, PID=0x%04x\n",
                    ctl->getModelName().c_str(),
                    properties.vendor_id, properties.product_id);
      myControllers[i] = ctl;
      foundEmptySlot = true;
      break;
    }
  }
  if (!foundEmptySlot) Serial.println("\nCALLBACK: no empty slot");
}

void onDisconnectedController(ControllerPtr ctl) {
  for (int i = 0; i < BP32_MAX_GAMEPADS; i++) {
    if (myControllers[i] == ctl) {
      Serial.printf("\nCALLBACK: Controller disconnected index=%d\n", i);
      myControllers[i] = nullptr;
      return;
    }
  }
  Serial.println("\nCALLBACK: disconnected, not found");
}

void processControllers() {
  for (auto myController : myControllers) {
    if (myController && myController->isConnected() && myController->hasData()) {
      if (myController->isGamepad()) processGamepad(myController);
      else Serial.println("\nUnsupported controller");
    }
  }
}

// --- Hilfsfunktionen ---
int axisToPwm(int axisValue) {
  int a = abs(axisValue);
  if (a < ANALOG_DEADZONE) return 0;
  int range = AXIS_MAX - ANALOG_DEADZONE;
  if (range <= 0) return 0;
  int pwm = (a - ANALOG_DEADZONE) * 255 / range;
  if (pwm > 255) pwm = 255;
  return pwm;
}

int axisToPwmRaw(int axisValue) {
  int a = abs(axisValue);
  if (a < ANALOG_DEADZONE) return 0;
  int range = AXIS_MAX - ANALOG_DEADZONE;
  int pwm = (a - ANALOG_DEADZONE) * 255 / range;
  if (pwm > 255) pwm = 255;
  return pwm;
}

void setMotor(int chFwd, int chRev, int axisValue) {
  int pwm = axisToPwm(axisValue);
  if (pwm == 0) {
    ledcWrite(chFwd, 0);
    ledcWrite(chRev, 0);
  } else if (axisValue > 0) {
    ledcWrite(chFwd, pwm);
    ledcWrite(chRev, 0);
  } else {
    ledcWrite(chFwd, 0);
    ledcWrite(chRev, pwm);
  }
}

void processPivot(int axisXValue) {
  setMotor(CH_PIVOT0, CH_PIVOT1, axisXValue);
}

void processBoom(int axisRYValue) {
  setMotor(CH_BOOM0, CH_BOOM1, axisRYValue);
}

void processThumbAndAuxAttach(ControllerPtr ctl) {
  // Thumb: Quadrat (ctl->x()) -> thumb0, Dreieck (ctl->y()) -> thumb1
  if (ctl->x()) {
    ledcWrite(CH_THUMB0, 255);
    ledcWrite(CH_THUMB1, 0);
  } else if (ctl->y()) {
    ledcWrite(CH_THUMB0, 0);
    ledcWrite(CH_THUMB1, 255);
  } else {
    ledcWrite(CH_THUMB0, 0);
    ledcWrite(CH_THUMB1, 0);
  }

  // AuxAttach: L2 -> auxAttach0, R2 -> auxAttach1
  if (ctl->l2()) {
    ledcWrite(CH_AUXA0, 255);
    ledcWrite(CH_AUXA1, 0);
  } else if (ctl->r2()) {
    ledcWrite(CH_AUXA0, 0);
    ledcWrite(CH_AUXA1, 255);
  } else {
    ledcWrite(CH_AUXA0, 0);
    ledcWrite(CH_AUXA1, 0);
  }
}

// --- Doppelklick-Erkennung: Kreuz-Taste (ctl->a()) ---
void checkModeToggle(ControllerPtr ctl) {
  bool btnNow = ctl->a();   // Kreuz
  if (btnNow && !lastModeBtn) {
    uint32_t now = millis();
    if ((now - lastModePressTime) < DOUBLE_CLICK_MS && lastModePressTime != 0) {
      joyTrackMode = !joyTrackMode;
      Serial.printf("\nMODE: %s\n", joyTrackMode ? "JOYSTICK-TRACK" : "NORMAL");
      lastModePressTime = 0;
    } else {
      lastModePressTime = now;
    }
  }
  lastModeBtn = btnNow;
}

// --- ESP2 via Serial: Dipper ---
void sendDipper(int axisYValue) {
  int a = abs(axisYValue);
  int cmd, speed;
  if (a < ANALOG_DEADZONE) {
    cmd = 9; speed = 0;
  } else {
    int range = AXIS_MAX - ANALOG_DEADZONE;
    speed = (a - ANALOG_DEADZONE) * 255 / range;
    if (speed > 255) speed = 255;
    speed = (speed / 5) * 5;
    cmd = (axisYValue > 0) ? 7 : 8;
  }
  if (cmd != lastDipperCmd || speed != lastDipperSpeed) {
    if (cmd == 9) Serial.printf("%d\n", cmd);
    else          Serial.printf("%d,%d\n", cmd, speed);
    lastDipperCmd = cmd;
    lastDipperSpeed = speed;
  }
}

// --- ESP2 via Serial: TiltAttach ---
void sendTiltAttach(int axisRXValue) {
  int a = abs(axisRXValue);
  int cmd, speed;
  if (a < ANALOG_DEADZONE) {
    cmd = 12; speed = 0;
  } else {
    int range = AXIS_MAX - ANALOG_DEADZONE;
    speed = (a - ANALOG_DEADZONE) * 255 / range;
    if (speed > 255) speed = 255;
    speed = (speed / 5) * 5;
    cmd = (axisRXValue > 0) ? 10 : 11;
  }
  if (cmd != lastTiltCmd || speed != lastTiltSpeed) {
    if (cmd == 12) Serial.printf("%d\n", cmd);
    else           Serial.printf("%d,%d\n", cmd, speed);
    lastTiltCmd = cmd;
    lastTiltSpeed = speed;
  }
}

// --- ESP2 via Serial: einzelne Track-Seite ---
void sendTrackSide(int cmdFwd, int cmdBack, int cmdStop, int speedSigned,
                   int &lastCmd, int &lastSpeed) {
  int cmd, speed;
  if (speedSigned == 0) {
    cmd = cmdStop; speed = 0;
  } else {
    speed = abs(speedSigned);
    if (speed > 255) speed = 255;
    speed = (speed / 5) * 5;
    cmd = (speedSigned > 0) ? cmdFwd : cmdBack;
  }
  if (cmd != lastCmd || speed != lastSpeed) {
    if (cmd == cmdStop) Serial.printf("%d\n", cmd);
    else                Serial.printf("%d,%d\n", cmd, speed);
    lastCmd = cmd;
    lastSpeed = speed;
  }
}

// --- Tracks im Normalmodus: L1 = links vorwärts, R1 = rechts vorwärts ---
void sendTracksNormal(ControllerPtr ctl) {
  int leftSpeed  = ctl->l1() ? 255 : 0;
  int rightSpeed = ctl->r1() ? 255 : 0;
  sendTrackSide(2, 1, 3, leftSpeed,  lastTrackLCmd, lastTrackLSpeed);
  sendTrackSide(5, 4, 6, rightSpeed, lastTrackRCmd, lastTrackRSpeed);
}

// --- Tracks im Joystick-Modus: rechter Stick = Panzersteuerung ---
void sendTracksJoystick(int axisRX, int axisRY) {
  int fwd  = -axisRY;
  int turn =  axisRX;

  int leftRaw  = fwd + turn;
  int rightRaw = fwd - turn;

  if (leftRaw  >  AXIS_MAX) leftRaw  =  AXIS_MAX;
  if (leftRaw  < -AXIS_MAX) leftRaw  = -AXIS_MAX;
  if (rightRaw >  AXIS_MAX) rightRaw =  AXIS_MAX;
  if (rightRaw < -AXIS_MAX) rightRaw = -AXIS_MAX;

  int leftPwm  = axisToPwmRaw(leftRaw);
  int rightPwm = axisToPwmRaw(rightRaw);
  if (leftRaw  < 0) leftPwm  = -leftPwm;
  if (rightRaw < 0) rightPwm = -rightPwm;

  sendTrackSide(2, 1, 3, leftPwm,  lastTrackLCmd, lastTrackLSpeed);
  sendTrackSide(5, 4, 6, rightPwm, lastTrackRCmd, lastTrackRSpeed);
}

// --- Licht ---
void processLights(ControllerPtr ctl) {
  if ((millis() - lightSwitchTime) > 200) {
    if (ctl->thumbR() == 1) {
      cabLightsOn = !cabLightsOn;
      digitalWrite(cabLights, cabLightsOn ? HIGH : LOW);
      delay(80);
    }
    if (ctl->thumbL() == 1) {
      auxLightsOn = !auxLightsOn;
      digitalWrite(auxLights, auxLightsOn ? HIGH : LOW);
      delay(80);
    }
    lightSwitchTime = millis();
  }
}

// --- Servos via D-Pad ---
void processServos(ControllerPtr ctl) {
  uint8_t dpad = ctl->dpad();
  moveClawServoUp   = (dpad & DPAD_UP);
  moveClawServoDown = (dpad & DPAD_DOWN);
  moveAuxServoUp    = (dpad & DPAD_LEFT);
  moveAuxServoDown  = (dpad & DPAD_RIGHT);

  if (moveClawServoUp) {
    if (servoDelay == 2) {
      if (clawServoValue < 170) clawServoValue += 2;
      clawServo.write(clawServoValue);
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveClawServoDown) {
    if (servoDelay == 2) {
      if (clawServoValue > 10) clawServoValue -= 2;
      clawServo.write(clawServoValue);
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveAuxServoUp) {
    if (servoDelay == 2) {
      if (auxServoValue < 170) auxServoValue += 2;
      auxServo.write(auxServoValue);
      servoDelay = 0;
    }
    servoDelay++;
  }
  if (moveAuxServoDown) {
    if (servoDelay == 2) {
      if (auxServoValue > 10) auxServoValue -= 2;
      auxServo.write(auxServoValue);
      servoDelay = 0;
    }
    servoDelay++;
  }
}

#if DEBUG_AXES
void debugAxes(ControllerPtr ctl) {
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg > 1000) {
    Serial.printf("DBG X=%4d Y=%4d RX=%4d RY=%4d | A=%d B=%d X=%d Y=%d | L1=%d L2=%d R1=%d R2=%d dpad=%d mode=%s\n",
                  ctl->axisX(), ctl->axisY(), ctl->axisRX(), ctl->axisRY(),
                  ctl->a(), ctl->b(), ctl->x(), ctl->y(),
                  ctl->l1(), ctl->l2(), ctl->r1(), ctl->r2(), ctl->dpad(),
                  joyTrackMode ? "JOY" : "NRM");
    lastDbg = millis();
  }
}
#endif

void processGamepad(ControllerPtr ctl) {
#if DEBUG_AXES
  debugAxes(ctl);
#endif

  // Modus-Umschaltung per Kreuz-Doppelklick (ctl->a())
  checkModeToggle(ctl);

  // Lokale Motoren an ESP1 (immer aktiv)
  processPivot(ctl->axisX());
  processBoom(ctl->axisRY());
  processThumbAndAuxAttach(ctl);

  // Dipper immer über linken Stick Y
  sendDipper(ctl->axisY());

  if (joyTrackMode) {
    // Joystick-Modus: rechter Stick steuert Ketten, TiltAttach pausiert
    sendTracksJoystick(ctl->axisRX(), ctl->axisRY());
    if (lastTiltCmd != 12) {
      Serial.println("12");
      lastTiltCmd = 12;
      lastTiltSpeed = 0;
    }
  } else {
    // Normalmodus: rechter Stick X = TiltAttach, L1/R1 = Tracks vorwärts
    sendTiltAttach(ctl->axisRX());
    sendTracksNormal(ctl);
  }

  processLights(ctl);
  processServos(ctl);
}

void setup() {
  Serial.begin(115200);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  ledcSetup(CH_PIVOT0, PWM_FREQ, PWM_RES); ledcAttachPin(pivot0, CH_PIVOT0);
  ledcSetup(CH_PIVOT1, PWM_FREQ, PWM_RES); ledcAttachPin(pivot1, CH_PIVOT1);
  ledcSetup(CH_BOOM0,  PWM_FREQ, PWM_RES); ledcAttachPin(mainBoom0, CH_BOOM0);
  ledcSetup(CH_BOOM1,  PWM_FREQ, PWM_RES); ledcAttachPin(mainBoom1, CH_BOOM1);
  ledcSetup(CH_THUMB0, PWM_FREQ, PWM_RES); ledcAttachPin(thumb0, CH_THUMB0);
  ledcSetup(CH_THUMB1, PWM_FREQ, PWM_RES); ledcAttachPin(thumb1, CH_THUMB1);
  ledcSetup(CH_AUXA0,  PWM_FREQ, PWM_RES); ledcAttachPin(auxAttach0, CH_AUXA0);
  ledcSetup(CH_AUXA1,  PWM_FREQ, PWM_RES); ledcAttachPin(auxAttach1, CH_AUXA1);

  ledcWrite(CH_PIVOT0, 0); ledcWrite(CH_PIVOT1, 0);
  ledcWrite(CH_BOOM0,  0); ledcWrite(CH_BOOM1,  0);
  ledcWrite(CH_THUMB0, 0); ledcWrite(CH_THUMB1, 0);
  ledcWrite(CH_AUXA0,  0); ledcWrite(CH_AUXA1,  0);

  pinMode(cabLights, OUTPUT);
  pinMode(auxLights, OUTPUT);
  digitalWrite(cabLights, LOW);
  digitalWrite(auxLights, LOW);

  clawServo.attach(clawServoPin);
  auxServo.attach(auxServoPin);
  clawServo.write(clawServoValue);
  auxServo.write(auxServoValue);

  Serial.printf("Firmware: %s\n", BP32.firmwareVersion());
  const uint8_t *addr = BP32.localBdAddress();
  Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n",
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.forgetBluetoothKeys();
  BP32.enableVirtualDevice(false);
}

void loop() {
  if (BP32.update()) processControllers();
  else vTaskDelay(1);
}