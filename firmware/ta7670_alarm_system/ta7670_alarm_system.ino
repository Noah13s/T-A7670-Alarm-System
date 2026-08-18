/**
 * @file      ta7670_alarm_system.ino
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2023  Shenzhen Xin Yuan Electronic Technology Co., Ltd
 * @date      2023-10-26
 *
 */
#include "utilities.h"
#include "Arduino.h"
#include <Preferences.h>
#include <WiFi.h>

#include "soc/rtc_cntl_reg.h"
#include "soc/soc.h"

#define SIM_PIN "1234"

Preferences prefs;
String line = "";

bool armed = false;
bool alarmTriggered = false;
int vibrations = 0;
String emergencyContact = "";

// Set from the modem's unsolicited output while a call is in progress (see pumpModemSerial()).
// Reset before each call attempt in callAlert().
bool callWasAnswered = false;  // AT+CLCC reported state 0 (active) at least once
bool modemCallEnded = false;   // modem reported "NO CARRIER" (call ended, whatever the reason)

// The alarm only performs lightweight GPIO checks and AT-command parsing, so running
// the ESP32 at 240 MHz wastes battery without improving response time. 80 MHz still
// comfortably handles the 115200-baud modem UART and all alarm processing.
const uint32_t POWER_SAVING_CPU_MHZ = 80;

// Yield briefly on every pass instead of keeping a CPU core busy continuously. This
// remains far shorter than VIBRATION_CONFIRM, so vibration detection stays responsive.
const uint32_t MAIN_LOOP_IDLE_MS = 2;

void powerSavingSetup() {
  // Neither radio is used by this project; keep them explicitly disabled even if a
  // future Arduino core or boot configuration leaves one of them initialized.
  WiFi.mode(WIFI_OFF);
#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
  btStop();
#endif

  if (setCpuFrequencyMhz(POWER_SAVING_CPU_MHZ)) {
    Serial.printf("CPU frequency reduced to %u MHz\n", getCpuFrequencyMhz());
  } else {
    Serial.println("Failed to reduce CPU frequency; continuing at the current speed");
  }
}

uint32_t AutoBaud() {
  static uint32_t rates[] = { 115200, 9600, 57600, 38400, 19200, 74400, 74880,
                              230400, 460800, 2400, 4800, 14400, 28800 };
  for (uint8_t i = 0; i < sizeof(rates) / sizeof(rates[0]); i++) {
    uint32_t rate = rates[i];
    Serial.printf("Trying baud rate %u\n", rate);
    SerialAT.updateBaudRate(rate);
    delay(10);
    for (int j = 0; j < 10; j++) {
      SerialAT.print("AT\r\n");
      String input = SerialAT.readString();
      if (input.indexOf("OK") >= 0) {
        Serial.printf("Modem responded at rate:%u\n", rate);
        return rate;
      }
    }
  }
  SerialAT.updateBaudRate(115200);
  return 0;
}

void setup() {
  Serial.begin(115200);                       // Set console baud rate
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 1);  // enable brownout detector

  Serial.println("Start Sketch");

  powerSavingSetup();

  batterySetup();
  alarmSetup();
  sensorSetup();

  prefs.begin("alarm", false);
  armed = prefs.getBool("armed", false);
  alarmTriggered = prefs.getBool("alarmTriggered", false);
  emergencyContact = prefs.getString("emergency", "");

  SerialAT.begin(115200, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);

#ifdef BOARD_POWERON_PIN
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

  // Set modem reset pin ,reset modem
#ifdef MODEM_RESET_PIN
  pinMode(MODEM_RESET_PIN, OUTPUT);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
  delay(100);
  digitalWrite(MODEM_RESET_PIN, MODEM_RESET_LEVEL);
  delay(2600);
  digitalWrite(MODEM_RESET_PIN, !MODEM_RESET_LEVEL);
#endif

  pinMode(BOARD_PWRKEY_PIN, OUTPUT);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, HIGH);
  delay(100);
  digitalWrite(BOARD_PWRKEY_PIN, LOW);

  if (AutoBaud()) {
    Serial.println(F("***********************************************************"));
    Serial.println(F(" You can now send AT commands"));
    Serial.println(F(" Enter \"AT\" (without quotes), and you should see \"OK\""));
    Serial.println(F(" If it doesn't work, select \"Both NL & CR\" in Serial Monitor"));
    Serial.println(F(" DISCLAIMER: Entering AT commands without knowing what they do"));
    Serial.println(F(" can have undesired consiquinces..."));
    Serial.println(F("***********************************************************\n"));

    checkSIM();
  } else {
    Serial.println(F("***********************************************************"));
    Serial.println(F(" Failed to connect to the modem! Check the baud and try again."));
    Serial.println(F("***********************************************************\n"));
  }
}

// Reads and processes whatever the modem has sent so far (notably +CMTI for new SMS).
// Kept as its own function so long blocking sequences (like the alarm phone calls)
// can call it too, instead of only the main loop.
void pumpModemSerial() {
  while (SerialAT.available()) {
    char c = SerialAT.read();
    Serial.write(c);

    if (c == '\n') {
      line.trim();

      // detect new SMS
      if (line.startsWith("+CMTI:")) {
        int idx = line.lastIndexOf(',');
        int smsIndex = line.substring(idx + 1).toInt();

        readSMS(smsIndex);
      }

      // call ended, whether it was answered-then-hung-up, rejected, or we hung up ourselves
      else if (line == "NO CARRIER") {
        modemCallEnded = true;
      }

      // response to our periodic "AT+CLCC" status check during a call
      // format: +CLCC: <id>,<dir>,<stat>,<mode>,<mpty>[,<number>,<type>]
      // stat 0 = active -> the call was picked up
      else if (line.startsWith("+CLCC:")) {
        int c1 = line.indexOf(',');
        int c2 = line.indexOf(',', c1 + 1);
        int c3 = line.indexOf(',', c2 + 1);
        if (c2 != -1 && c3 != -1) {
          int stat = line.substring(c2 + 1, c3).toInt();
          if (stat == 0) {
            callWasAnswered = true;
          }
        }
      }

      line = "";
    } else {
      line += c;
    }
  }
}

void loop() {
  sensorLoop();
  pumpModemSerial();
  batteryLoop();

  while (Serial.available()) {
    SerialAT.write(Serial.read());
  }

  delay(MAIN_LOOP_IDLE_MS);
}
