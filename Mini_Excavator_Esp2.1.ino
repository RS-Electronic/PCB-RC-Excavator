// ESP2_1-24_Motor_Slave_S3SuperMini.txt
// ESP32-S3 SuperMini - Motor-Slave für Bagger 1:24
// Steuert: LeftTrack, RightTrack, Dipper, TiltAttach über 4x DRV8833
// Bekommt Befehle von ESP1 über Serial (UART)
//
// Protokoll (von ESP1):
//   1,vel   LeftTrack rückwärts   2,vel  LeftTrack vorwärts   3  LeftTrack stop
//   4,vel   RightTrack rückwärts  5,vel  RightTrack vorwärts  6  RightTrack stop
//   7,vel   Dipper up             8,vel  Dipper down           9  Dipper stop
//   10,vel  TiltAttach up        11,vel  TiltAttach down      12  TiltAttach stop

#include <Arduino.h>

// Pin-Mapping für ESP32-S3 SuperMini
// Tracks (DRV3)
#define LEFT_TRACK0   1   // PIN1  -> DRV3-A IN1 Linker Kettenantrieb
#define LEFT_TRACK1   2   // PIN2  -> DRV3-A IN2 Linker Kettenantrieb
#define RIGHT_TRACK0  3   // PIN3  -> DRV3-B IN1 Rechter Kettenantrieb
#define RIGHT_TRACK1  4   // PIN4  -> DRV3-B IN2 Rechter Kettenantrieb

// Dipper + TiltAttach (DRV4)
#define DIPPER0       5   // PIN5  -> DRV4-A IN1 Löffelstiel auf
#define DIPPER1       6   // PIN6  -> DRV4-A IN2 Löffelstiel ab
#define TILT0         7   // PIN7  -> DRV4-B IN1 Schwenklöffel
#define TILT1         8   // PIN8  -> DRV4-B IN2 Schwenklöffel

// Optionale Aux-Motoren
#define AUX_MOTOR0    9
#define AUX_MOTOR1    10
#define AUX_MOTOR2    11
#define AUX_MOTOR3    12

// Status-LED
#define STATUS_LED    48

// PWM-Kanäle (LEDC) - S3 hat 8 Kanäle (0..7), alle hier belegt
#define PWM_FREQ      20000
#define PWM_RES       8

#define CH_LT0        0
#define CH_LT1        1
#define CH_RT0        2
#define CH_RT1        3
#define CH_DIP0       4
#define CH_DIP1       5
#define CH_TILT0      6
#define CH_TILT1      7

String receivedDataStr = "";

void setup() {
  Serial.begin(115200);

  // PWM-Setup für alle Motor-Pins
  ledcSetup(CH_LT0,   PWM_FREQ, PWM_RES); ledcAttachPin(LEFT_TRACK0,  CH_LT0);
  ledcSetup(CH_LT1,   PWM_FREQ, PWM_RES); ledcAttachPin(LEFT_TRACK1,  CH_LT1);
  ledcSetup(CH_RT0,   PWM_FREQ, PWM_RES); ledcAttachPin(RIGHT_TRACK0, CH_RT0);
  ledcSetup(CH_RT1,   PWM_FREQ, PWM_RES); ledcAttachPin(RIGHT_TRACK1, CH_RT1);
  ledcSetup(CH_DIP0,  PWM_FREQ, PWM_RES); ledcAttachPin(DIPPER0,      CH_DIP0);
  ledcSetup(CH_DIP1,  PWM_FREQ, PWM_RES); ledcAttachPin(DIPPER1,      CH_DIP1);
  ledcSetup(CH_TILT0, PWM_FREQ, PWM_RES); ledcAttachPin(TILT0,        CH_TILT0);
  ledcSetup(CH_TILT1, PWM_FREQ, PWM_RES); ledcAttachPin(TILT1,        CH_TILT1);

  // Alle Kanäle aus
  ledcWrite(CH_LT0, 0);   ledcWrite(CH_LT1, 0);
  ledcWrite(CH_RT0, 0);   ledcWrite(CH_RT1, 0);
  ledcWrite(CH_DIP0, 0);  ledcWrite(CH_DIP1, 0);
  ledcWrite(CH_TILT0, 0); ledcWrite(CH_TILT1, 0);

  // Aux-Pins digital (nur Reserve)
  pinMode(AUX_MOTOR0, OUTPUT);
  pinMode(AUX_MOTOR1, OUTPUT);
  pinMode(AUX_MOTOR2, OUTPUT);
  pinMode(AUX_MOTOR3, OUTPUT);
  digitalWrite(AUX_MOTOR0, LOW);
  digitalWrite(AUX_MOTOR1, LOW);
  digitalWrite(AUX_MOTOR2, LOW);
  digitalWrite(AUX_MOTOR3, LOW);

  pinMode(STATUS_LED, OUTPUT);
  digitalWrite(STATUS_LED, LOW);

  Serial.println("ESP2 ready");
}

// Motor über zwei PWM-Kanäle mit Vorzeichen steuern
void setMotorPwm(int chFwd, int chRev, int signedSpeed) {
  if (signedSpeed == 0) {
    ledcWrite(chFwd, 0);
    ledcWrite(chRev, 0);
  } else if (signedSpeed > 0) {
    if (signedSpeed > 255) signedSpeed = 255;
    ledcWrite(chFwd, signedSpeed);
    ledcWrite(chRev, 0);
  } else {
    int s = -signedSpeed;
    if (s > 255) s = 255;
    ledcWrite(chFwd, 0);
    ledcWrite(chRev, s);
  }
}

void loop() {
  if (Serial.available() > 0) {
    digitalWrite(STATUS_LED, HIGH);

    receivedDataStr = Serial.readStringUntil('\n');
    receivedDataStr.trim();
    if (receivedDataStr.length() == 0) {
      digitalWrite(STATUS_LED, LOW);
      return;
    }

    int commaIndex = receivedDataStr.indexOf(',');

    if (commaIndex != -1) {
      // Format "mtr,velocity"
      int mtr = receivedDataStr.substring(0, commaIndex).toInt();
      int velocity = receivedDataStr.substring(commaIndex + 1).toInt();

      switch (mtr) {
        // LeftTrack
        case 2: setMotorPwm(CH_LT0, CH_LT1,  velocity); break;   // vorwärts
        case 1: setMotorPwm(CH_LT0, CH_LT1, -velocity); break;   // rückwärts
        // RightTrack
        case 5: setMotorPwm(CH_RT0, CH_RT1,  velocity); break;   // vorwärts
        case 4: setMotorPwm(CH_RT0, CH_RT1, -velocity); break;   // rückwärts
        // Dipper
        case 7: setMotorPwm(CH_DIP0, CH_DIP1,  velocity); break; // up
        case 8: setMotorPwm(CH_DIP0, CH_DIP1, -velocity); break; // down
        // TiltAttach
        case 10: setMotorPwm(CH_TILT0, CH_TILT1,  velocity); break;
        case 11: setMotorPwm(CH_TILT0, CH_TILT1, -velocity); break;
        default: break;
      }
    } else {
      // Format "mtr" (Stop)
      int mtr = receivedDataStr.toInt();
      switch (mtr) {
        case 3:  setMotorPwm(CH_LT0,   CH_LT1,   0); break;  // LeftTrack stop
        case 6:  setMotorPwm(CH_RT0,   CH_RT1,   0); break;  // RightTrack stop
        case 9:  setMotorPwm(CH_DIP0,  CH_DIP1,  0); break;  // Dipper stop
        case 12: setMotorPwm(CH_TILT0, CH_TILT1, 0); break;  // TiltAttach stop
        default: break;
      }
    }

    digitalWrite(STATUS_LED, LOW);
  }
}