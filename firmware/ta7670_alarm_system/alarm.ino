const int mosfetSignalPin = 32;  // GPIO connected to HW-042 signal pin

void alarmSetup() {
  pinMode(mosfetSignalPin, OUTPUT);
  digitalWrite(mosfetSignalPin, LOW);
  Serial.println("Alarm ready");
}

void toggleAlarm(bool val) {
  if (val) {
    digitalWrite(mosfetSignalPin, HIGH);
  } else {
    digitalWrite(mosfetSignalPin, LOW);
  }
}

void alarmBip() {
  digitalWrite(mosfetSignalPin, HIGH);
  delay(100);
  digitalWrite(mosfetSignalPin, LOW);
}

void alarmRing1() {
  alarmBip();
  delay(50);
  alarmBip();
  delay(50);
  alarmBip();
}

void alarmRing2() {
  alarmBip();
  delay(50);
  alarmBip();
}