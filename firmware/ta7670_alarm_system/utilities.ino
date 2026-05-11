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
        }
      }
    } else {
      Serial.println("Failed to unlock SIM");
    }
  } else {
    Serial.println("Unknown SIM state");
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
    sendSMS(sender, "System armed");
    deleteSMS(index);
  }

  else if (body == "DISARM") {
    armed = false;
    prefs.putBool("armed", armed);
    sendSMS(sender, "System disarmed");
    deleteSMS(index);
  }

  else if (body == "STATUS") {

    String status = getSMSStatus();
    String state =
      armed ? "ARMED" : "DISARMED";

    sendSMS(
      sender,
      "State: " + state + "\nSMS: " + status);
    deleteSMS(index);
  }

  else if (body == "CLEAR") {
    deleteAllSMS();
    sendSMS(sender, "All messages cleared");
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