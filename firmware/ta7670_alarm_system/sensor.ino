#define SENSOR_PIN 33
// NOTE: on the T-A7670 board (utilities.h), pin 33 is also MODEM_RING_PIN.
// If the modem ever drives that pin, it will look like a false vibration.
// Move the sensor to a free GPIO if you see unexplained triggers.

// Vibration counting window: detections inside this window are summed together
const unsigned long VIBRATION_TIMEOUT = 5000;    // 5 seconds
// Minimum time between two vibrations to be counted as two separate events
const unsigned long VIBRATION_DEBOUNCE = 300;    // 300 ms
// The signal must stay HIGH this long to be treated as a real vibration and not electrical noise
const unsigned long VIBRATION_CONFIRM = 30;      // 30 ms
// Number of vibrations inside VIBRATION_TIMEOUT needed to trigger the alarm
const int VIBRATION_THRESHOLD = 3;
// How long the siren stays on once triggered
const unsigned long ALARM_DURATION = 30000;      // 30 seconds

// Phone call escalation once the alarm is triggered
const int ALARM_CALL_COUNT = 4;
// Kept short on purpose: many carriers (this was observed with Free Mobile) auto-forward
// an unanswered call to voicemail after roughly 20 seconds. If our own hangup lands right
// as that happens, the modem ends up "connected" to voicemail instead of a clean unanswered
// call, and the line then takes much longer to release before the next attempt can dial.
// Staying well under that threshold means we hang up while it's still just ringing.
const unsigned long ALARM_CALL_RING_DURATION = 12000; // ring time per call attempt
const unsigned long ALARM_CALL_GAP = 2000;            // pause between call attempts

unsigned long lastVibrationTime = 0;
unsigned long alarmStartTime = 0;
unsigned long sensorHighSince = 0;

bool vibrationDetected = false;
bool sensorCandidate = false;

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
    vibrationDetected = false;
    sensorCandidate = false;
    return;
  }

  int sensorState = digitalRead(SENSOR_PIN);
  unsigned long now = millis();

  if (sensorState == HIGH) {
    if (!vibrationDetected) {
      // require the pin to stay HIGH for VIBRATION_CONFIRM ms to reject short noise spikes
      if (!sensorCandidate) {
        sensorCandidate = true;
        sensorHighSince = now;
      } else if (now - sensorHighSince >= VIBRATION_CONFIRM) {
        // debounce: ignore if it comes right after the previous counted vibration
        if (now - lastVibrationTime >= VIBRATION_DEBOUNCE) {
          vibrationDetected = true;
          sensorCandidate = false;

          vibrations++;
          lastVibrationTime = now;

          alarmBip();
          notifyVibration();

          if (vibrations >= VIBRATION_THRESHOLD) {
            triggerAlarm();
            vibrations = 0;
          }
        }
      }
    }
  } else {
    vibrationDetected = false;
    sensorCandidate = false;
  }

  if (vibrations > 0 && now - lastVibrationTime > VIBRATION_TIMEOUT) {
    vibrations = 0;
  }
}

// Sends one SMS per detected vibration, before the alarm threshold is reached
void notifyVibration() {
  if (emergencyContact.length() == 0) return;

  sendSMS(emergencyContact,
          "Vibration detected (" + String(vibrations) + "/" + String(VIBRATION_THRESHOLD) + ")");
}

// Called once the vibration threshold is reached: sounds the siren, sends an SMS,
// then places ALARM_CALL_COUNT phone calls to the emergency contact
void triggerAlarm() {
  alarmTriggered = true;
  toggleAlarm(true);

  if (emergencyContact.length() > 0) {
    sendSMS(emergencyContact, "ALARM TRIGGERED! Vibration threshold reached.");
    callAlert(emergencyContact, ALARM_CALL_COUNT, ALARM_CALL_RING_DURATION, ALARM_CALL_GAP);
  }

  // Start the auto shut-off countdown once notifications/calls are done,
  // so the siren keeps sounding for the full ALARM_DURATION after that.
  alarmStartTime = millis();
}
