#include <esp32-hal-adc.h>

#ifndef BOARD_BAT_ADC_PIN
#error "Battery ADC not supported on this board"
#endif

// --- NEW VARIABLES FOR MONITORING ---
unsigned long lastBatteryCheckTime = 0;
const unsigned long BATTERY_CHECK_INTERVAL = 60000; // Check every 60 seconds
int lastNotifiedThreshold = 100; 

void batterySetup() {
#ifdef BOARD_POWERON_PIN
  pinMode(BOARD_POWERON_PIN, OUTPUT);
  digitalWrite(BOARD_POWERON_PIN, HIGH);
#endif

  analogSetAttenuation(ADC_11db);
  analogReadResolution(12);

#if CONFIG_IDF_TARGET_ESP32
  analogSetWidth(12);
#endif
}

int getBatteryPercent() {
  int batteryPercent = map(getBatteryMv(), 3000, 4200, 0, 100);
  batteryPercent = constrain(batteryPercent, 0, 100);
  return batteryPercent;
}

uint32_t getBatteryMv() {
  uint32_t batteryMv = analogReadMilliVolts(BOARD_BAT_ADC_PIN);
  batteryMv *= 2;  // voltage divider compensation
  float batteryV = batteryMv / 1000.0;
  return batteryMv;
}

// --- NEW FUNCTION: MONITORING LOOP ---
void batteryLoop() {
  unsigned long now = millis();
  
  // Check the battery at the specified interval
  if (now - lastBatteryCheckTime >= BATTERY_CHECK_INTERVAL) {
    lastBatteryCheckTime = now;
    
    // Do nothing if no emergency contact is configured
    if (emergencyContact.length() == 0) return;

    int currentPercent = getBatteryPercent();

    // 10% Critical Warning
    if (currentPercent <= 10 && lastNotifiedThreshold > 10) {
      sendSMS(emergencyContact, "WARNING: Battery critically low (" + String(currentPercent) + "%)");
      lastNotifiedThreshold = 10;
    } 
    // 20% Low Warning
    else if (currentPercent <= 20 && currentPercent > 10 && lastNotifiedThreshold > 20) {
      sendSMS(emergencyContact, "WARNING: Battery low (" + String(currentPercent) + "%)");
      lastNotifiedThreshold = 20;
    } 
    // Reset the tracking variable if the battery is charged
    else if (currentPercent > 20) {
      lastNotifiedThreshold = 100;
    }
  }
}