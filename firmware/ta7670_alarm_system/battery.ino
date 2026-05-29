#include <esp32-hal-adc.h>

#ifndef BOARD_BAT_ADC_PIN
#error "Battery ADC not supported on this board"
#endif

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