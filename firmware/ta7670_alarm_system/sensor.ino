#define SENSOR_PIN 33

const unsigned long VIBRATION_TIMEOUT = 5000; // 5 seconds
const unsigned long ALARM_DURATION = 30000; // 30 seconds

unsigned long lastVibrationTime = 0;
unsigned long alarmStartTime = 0;

bool vibrationDetected = false;

void sensorSetup() {
  pinMode(SENSOR_PIN, INPUT);
}

void sensorLoop() {
  // Alarm is currently sounding
  if (alarmTriggered) {
    if (!armed) {
      alarmTriggered = false;
      toggleAlarm(false);
      return;
    }

    if (millis() - alarmStartTime >= ALARM_DURATION) {
      alarmTriggered = false;
      toggleAlarm(false);
    }

    return;
  }

  if (!armed) {
    return;
  }

  int sensorState = digitalRead(SENSOR_PIN);

  if (sensorState == HIGH && !vibrationDetected) {
    vibrationDetected = true;

    vibrations++;
    lastVibrationTime = millis();

    alarmBip();

    if (vibrations >= 3) {
      alarmTriggered = true;
      alarmStartTime = millis();

      toggleAlarm(true);

      vibrations = 0;
    }
  }

  if (sensorState == LOW) {
    vibrationDetected = false;
  }

  if (vibrations > 0 &&
      millis() - lastVibrationTime > VIBRATION_TIMEOUT) {
    vibrations = 0;
  }
}