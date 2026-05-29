bool sendATCommand(const String &cmd, const String &expected, uint32_t timeout = 3000) {
  Serial.println(">> " + cmd);

  // Flush old data
  while (SerialAT.available()) {
    SerialAT.read();
  }

  SerialAT.println(cmd);

  uint32_t start = millis();
  String response;

  while (millis() - start < timeout) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }

    if (response.indexOf(expected) != -1) {
      Serial.println(response);
      return true;
    }
  }

  Serial.println(response);
  return false;
}

bool waitNetwork(uint32_t timeout = 30000) {
  uint32_t start = millis();

  while (millis() - start < timeout) {
    SerialAT.println("AT+CREG?");
    String r = "";

    uint32_t t = millis();
    while (millis() - t < 1000) {
      while (SerialAT.available()) {
        r += (char)SerialAT.read();
      }
    }

    Serial.println(r);

    if (r.indexOf("+CREG: 0,1") != -1 || r.indexOf("+CREG: 0,5") != -1) {
      return true;
    }

    delay(1000);
  }

  return false;
}

void checkSIM() {
  // Basic AT test
  if (sendATCommand("AT", "OK")) {
    Serial.println("AT test successful");
  } else {
    Serial.println("AT test failed");
    return;
  }

  // Check SIM state
  Serial.println("Checking SIM status...");

  while (SerialAT.available()) {
    SerialAT.read();
  }

  SerialAT.println("AT+CPIN?");

  uint32_t start = millis();
  String response;

  while (millis() - start < 5000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      response += c;
    }

    if (response.indexOf("OK") != -1 || response.indexOf("ERROR") != -1) {
      break;
    }
  }

  Serial.println(response);

  if (response.indexOf("+CPIN: READY") != -1) {
    Serial.println("SIM already unlocked");
  } else if (response.indexOf("+CPIN: SIM PIN") != -1) {
    Serial.println("SIM requires PIN, sending PIN...");

    String cmd = "AT+CPIN=";
    cmd += SIM_PIN;

    if (sendATCommand(cmd, "OK", 10000)) {
      Serial.println("SIM unlocked successfully");
      if (sendATCommand("AT+CMGF=1", "OK")) {
        Serial.println("SMS mode set to text");
        if (sendATCommand("AT+CNMI=2,1,0,0,0", "OK")) {
          Serial.println("Activated ESP32 SMS notification");
          if (sendATCommand("AT+CSCS=\"GSM\"", "OK")) {
            Serial.println("SMS charset set to GSM");
          }
        }
      }
    } else {
      Serial.println("Failed to unlock SIM");
    }
  } else {
    Serial.println("Unknown SIM state");
  }

  if (waitNetwork()) {
    if (emergencyContact.length() > 0) {
      String batteryPercent = String(getBatteryPercent());
      sendSMS(emergencyContact, "System started\nBattery Level : " + batteryPercent + "%");
    }
  } else {
    Serial.println("Network not ready, skipping boot SMS");
  }
}

void readSMS(int index) {
  String cmd = "AT+CMGR=" + String(index);

  SerialAT.println(cmd);

  String msg = "";
  uint32_t start = millis();

  while (millis() - start < 3000) {
    while (SerialAT.available()) {
      char c = SerialAT.read();
      msg += c;
    }
  }

  Serial.println("SMS RAW:");
  Serial.println(msg);

  handleSMS(msg, index);
}

void handleSMS(String msg, int index) {
  String sender;
  String body;

  int h = msg.indexOf("+CMGR:");
  if (h == -1) return;

  int headerEnd = msg.indexOf("\n", h);
  if (headerEnd == -1) return;

  int bodyEnd = msg.indexOf("\n", headerEnd + 1);
  if (bodyEnd == -1) return;

  int p1 = msg.indexOf("\",\"", h);
  if (p1 != -1) {
    int p2 = msg.indexOf("\"", p1 + 3);
    sender = msg.substring(p1 + 3, p2);
    sender.trim();
  }

  body = msg.substring(headerEnd + 1, bodyEnd);
  body.replace("\r", "");
  body.replace("\n", "");
  body.trim();
  body.toUpperCase();

  Serial.print("BODY=[");
  Serial.print(body);
  Serial.println("]");

  if (body == "ARM") {
    armed = true;
    prefs.putBool("armed", armed);
    alarmRing1();
    sendSMS(sender, "System armed");
    deleteSMS(index);
  }

  else if (body.startsWith("SETEMERGENCY")) {
    // format: SETEMERGENCY +1234567890
    int sp = body.indexOf(' ');
    if (sp != -1) {
      emergencyContact = body.substring(sp + 1);
      emergencyContact.trim();

      prefs.putString("emergency", emergencyContact);

      sendSMS(sender, "Emergency contact set");
    } else {
      sendSMS(sender, "Usage: SETEMERGENCY +number");
    }

    deleteSMS(index);
  }

  else if (body == "DISARM") {
    armed = false;
    prefs.putBool("armed", armed);
    alarmRing2();
    sendSMS(sender, "System disarmed");
    deleteSMS(index);
  }

  else if (body == "STATUS") {

    String status = getSMSStatus();
    String state =
      armed ? "ARMED" : "DISARMED";
    String emergency = emergencyContact.length() > 0
                         ? emergencyContact
                         : "not set";
    String batteryPercent = String(getBatteryPercent());

    sendSMS(
      sender,
      "State: " + state + "\nSMS: " + status + "\nEmergency: " + emergency + "\nBattery Level : " + batteryPercent + "%");
    deleteSMS(index);
  }

  else if (body == "CLEAR") {
    deleteAllSMS();
    sendSMS(sender, "All messages cleared");
  }

  else if (body == "ALARMTEST") {
    toggleAlarm(true);
    delay(2000);
    toggleAlarm(false);
    deleteSMS(index);
  }

  else if (body == "HELP") {
    sendSMS(
      sender,
      "Available commands:\n"
      "ARM\n"
      "DISARM\n"
      "STATUS\n"
      "SETEMERGENCY +number\n"
      "CLEAR\n"
      "HELP");

    deleteSMS(index);
  }

  else {
    sendSMS(sender, "Unknown command: " + body);
    deleteSMS(index);
  }
}

void sendSMS(String number, String text) {
  // flush input first
  while (SerialAT.available()) SerialAT.read();

  SerialAT.print("AT+CMGS=\"");
  SerialAT.print(number);
  SerialAT.println("\"");

  // wait for prompt '>'
  uint32_t start = millis();
  bool prompt = false;

  while (millis() - start < 5000) {
    if (SerialAT.available()) {
      char c = SerialAT.read();
      if (c == '>') {
        prompt = true;
        break;
      }
    }
  }

  if (!prompt) {
    Serial.println("No CMGS prompt");
    return;
  }

  delay(50);  // small safety gap

  SerialAT.print(text);
  SerialAT.write(26);

  // wait final response cleanly
  String resp = "";
  start = millis();

  while (millis() - start < 8000) {
    while (SerialAT.available()) {
      resp += (char)SerialAT.read();
    }

    if (resp.indexOf("+CMGS") != -1) break;
  }

  Serial.println(resp);
}

void deleteSMS(int index) {
  String cmd = "AT+CMGD=" + String(index);

  if (sendATCommand(cmd, "OK")) {
    Serial.println("SMS deleted: " + String(index));
  } else {
    Serial.println("Failed to delete SMS: " + String(index));
  }
}

void deleteAllSMS() {
  sendATCommand("AT+CMGD=1,4", "OK", 5000);
}

String getSMSStatus() {
  SerialAT.println("AT+CPMS?");

  String resp = "";
  uint32_t start = millis();

  while (millis() - start < 3000) {
    while (SerialAT.available()) {
      resp += (char)SerialAT.read();
    }
  }

  Serial.println(resp);

  // expected:
  // +CPMS: "SM",3,50,"SM",3,50,"SM",3,50
  int p = resp.indexOf("+CPMS:");
  if (p == -1) return "unknown";

  int firstComma = resp.indexOf(",", p);
  int secondComma = resp.indexOf(",", firstComma + 1);

  int used = resp.substring(firstComma + 1, secondComma).toInt();

  int thirdComma = resp.indexOf(",", secondComma + 1);
  int max = resp.substring(secondComma + 1, thirdComma).toInt();

  return String(used - 1) + "/" + String(max);
}
