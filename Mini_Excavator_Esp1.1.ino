// ESP1_1-24_Gamepad_Master.txt
// ESP32 #1 (Classic DevKit) - Gamepad Master für Bagger 1:24
// FW_VERSION: ESP32.1.V08
//
// Tastenbelegung PS4 (Bluepad32-Mapping):
//   Kreuz   = ctl->a()  -> X-Doppelklick = Moduswechsel
//   Kreis   = ctl->b()  -> frei
//   Quadrat = ctl->x()  -> FREI
//   Dreieck = ctl->y()  -> FREI
//   L1/R1   -> Schub-Schild: Kick 95% (0.08s) -> Rampe 70%..100% in 2s
//   L2/R2   -> Thumb:        Rampe 65%..100% in 2s (KEIN Kick)
//   D-Pad   -> clawServo (up/down), auxServo (left/right)
//
// Modi:
//   NORMAL:        rechter Stick Y = MainBoom, rechter Stick X = TiltAttach
//   JOYSTICK-TRACK: rechter Stick = Ketten (Panzersteuerung), MainBoom PAUSIERT

#include <Arduino.h>
#include <ESP32Servo.h>
#include <Bluepad32.h>

// ===== VERSION =====
#define FW_VERSION "ESP32.1.V08"

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

#define thumb0       4   // Thumb DRV2-A IN1 Baggerdaumen, BUTTON_SHOULDER_L2
#define thumb1       2   // Thumb DRV2-A IN2 Baggerdaumen, BUTTON_SHOULDER_R2
#define auxAttach0   25  // AuxAttach DRV2-B IN1 Schub-Schild, BUTTON_SHOULDER_L1
#define auxAttach1   26  // AuxAttach DRV2-B IN2 Schub-Schild, BUTTON_SHOULDER_R1

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

// --- Rampen-Parameter ---
// Thumb: nur Rampe, kein Kick
#define RAMP_START_PWM_THUMB  166   // 65% - Thumb Rampe Start
// Schub-Schild: Kick + Rampe
#define KICK_PWM_AUX          242   // 95% - Kick fuer Schub-Schild
#define KICK_DURATION_AUX_MS  80    // 0.08 s Kick
#define RAMP_START_PWM_AUX    179   // 70% - Schub-Schild Rampe Start (nach Kick)
// Gemeinsam
#define RAMP_END_PWM          255   // 100%
#define RAMP_DURATION_MS      2000  // 2 s Rampe

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

// --- Rampen-Status (Thumb) ---
uint32_t thumbRampStart = 0;
bool     thumbRampActive = false;
int      thumbDir = 0;

// --- Rampen-Status (AuxAttach) ---
uint32_t auxRampStart = 0;
bool     auxRampActive = false;
int      auxDir = 0;

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

// Rampe mit optionalem Kick:
//   kickDurationMs == 0  -> kein Kick, Rampe sofort ab rampStartPwm
//   kickDurationMs  > 0  -> Kick auf kickPwm fuer kickDurationMs,
//                           dann Rampe von rampStartPwm bis RAMP_END_PWM
int computeRampPwm(uint32_t rampStart, uint16_t kickDurationMs, int kickPwm, int rampStartPwm) {
  uint32_t elapsed = millis() - rampStart;

  // Phase 1: Kick (optional)
  if (kickDurationMs > 0 && elapsed < kickDurationMs) {
    return kickPwm;
  }

  // Phase 2: Rampe
  uint32_t rampElapsed = (kickDurationMs > 0) ? (elapsed - kickDurationMs) : elapsed;
  if (rampElapsed >= RAMP_DURATION_MS) return RAMP_END_PWM;

  long span = (long)RAMP_END_PWM - (long)rampStartPwm;
  long pwm = (long)rampStartPwm + (span * (long)rampElapsed) / (long)RAMP_DURATION_MS;
  if (pwm > RAMP_END_PWM) pwm = RAMP_END_PWM;
  if (pwm < rampStartPwm) pwm = rampStartPwm;
  return (int)pwm;
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

void stopBoom() {
  ledcWrite(CH_BOOM0, 0);
  ledcWrite(CH_BOOM1, 0);
}

void processThumbAndAuxAttach(ControllerPtr ctl) {
  // ---------- Thumb via L2 / R2 (nur Rampe, kein Kick) ----------
  bool l2 = ctl->l2();
  bool r2 = ctl->r2();
  int newThumbDir = 0;
  if (l2 && !r2) newThumbDir =  1;
  if (r2 && !l2) newThumbDir = -1;

  if (newThumbDir != thumbDir) {
    thumbDir = newThumbDir;
    if (thumbDir != 0) {
      thumbRampStart = millis();
      thumbRampActive = true;
    } else {
      thumbRampActive = false;
    }
  }

  int thumbPwm = thumbRampActive
               ? computeRampPwm(thumbRampStart, 0, 0, RAMP_START_PWM_THUMB)
               : 0;

  if (thumbPwm > 0 && thumbDir == 1) {
    ledcWrite(CH_THUMB0, thumbPwm);
    ledcWrite(CH_THUMB1, 0);
  } else if (thumbPwm > 0 && thumbDir == -1) {
    ledcWrite(CH_THUMB0, 0);
    ledcWrite(CH_THUMB1, thumbPwm);
  } else {
    ledcWrite(CH_THUMB0, 0);
    ledcWrite(CH_THUMB1, 0);
  }

  // ---------- AuxAttach via L1 / R1 (Kick + Rampe) ----------
  bool l1 = ctl->l1();
  bool r1 = ctl->r1();
  int newAuxDir = 0;
  if (l1 && !r1) newAuxDir =  1;
  if (r1 && !l1) newAuxDir = -1;

  if (newAuxDir != auxDir) {
    auxDir = newAuxDir;
    if (auxDir != 0) {
      auxRampStart = millis();
      auxRampActive = true;
    } else {
      auxRampActive = false;
    }
  }

  int auxPwm = auxRampActive
             ? computeRampPwm(auxRampStart, KICK_DURATION_AUX_MS, KICK_PWM_AUX, RAMP_START_PWM_AUX)
             : 0;

  if (auxPwm > 0 && auxDir == 1) {
    ledcWrite(CH_AUXA0, auxPwm);
    ledcWrite(CH_AUXA1, 0);
  } else if (auxPwm > 0 && auxDir == -1) {
    ledcWrite(CH_AUXA0, 0);
    ledcWrite(CH_AUXA1, auxPwm);
  } else {
    ledcWrite(CH_AUXA0, 0);
    ledcWrite(CH_AUXA1, 0);
  }
}

void checkModeToggle(ControllerPtr ctl) {
  bool btnNow = ctl->a();
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
  if (millis() - lastDbg > 500) {
    int thumbShow = thumbRampActive
                  ? computeRampPwm(thumbRampStart, 0, 0, RAMP_START_PWM_THUMB)
                  : 0;
    int auxShow   = auxRampActive
                  ? computeRampPwm(auxRampStart, KICK_DURATION_AUX_MS, KICK_PWM_AUX, RAMP_START_PWM_AUX)
                  : 0;
    Serial.printf("DBG X=%4d Y=%4d RX=%4d RY=%4d | L1=%d L2=%d R1=%d R2=%d | THUMB_PWM=%3d AUX_PWM=%3d | mode=%s\n",
                  ctl->axisX(), ctl->axisY(), ctl->axisRX(), ctl->axisRY(),
                  ctl->l1(), ctl->l2(), ctl->r1(), ctl->r2(),
                  thumbShow, auxShow,
                  joyTrackMode ? "JOY" : "NRM");
    lastDbg = millis();
  }
}
#endif

void processGamepad(ControllerPtr ctl) {
#if DEBUG_AXES
  debugAxes(ctl);
#endif

  checkModeToggle(ctl);

  processPivot(ctl->axisX());
  processThumbAndAuxAttach(ctl);
  sendDipper(ctl->axisY());

  if (joyTrackMode) {
    stopBoom();
    sendTracksJoystick(ctl->axisRX(), ctl->axisRY());
    if (lastTiltCmd != 12) {
      Serial.println("12");
      lastTiltCmd = 12;
      lastTiltSpeed = 0;
    }
  } else {
    processBoom(ctl->axisRY());
    sendTiltAttach(ctl->axisRX());
  }

  processLights(ctl);
  processServos(ctl);
}

void setup() {
  Serial.begin(115200);

  Serial.printf("\n===== %s booting =====\n", FW_VERSION);

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

  Serial.printf("Bluepad32 Firmware: %s\n", BP32.firmwareVersion());
  const uint8_t *addr = BP32.localBdAddress();
  Serial.printf("BD Addr: %2X:%2X:%2X:%2X:%2X:%2X\n",
                addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);
  Serial.printf("FW: %s | Modus: NORMAL\n", FW_VERSION);

  BP32.setup(&onConnectedController, &onDisconnectedController);
  BP32.forgetBluetoothKeys();
  BP32.enableVirtualDevice(false);
}

void loop() {
  if (BP32.update()) processControllers();
  else vTaskDelay(1);
}
