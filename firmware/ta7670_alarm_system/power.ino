#include "esp_sleep.h"
#include "driver/gpio.h"

const uint32_t POWER_SAVING_CPU_MHZ = 80;
const uint32_t MODEM_WAKE_DELAY_MS = 500;
const uint32_t MODEM_IDLE_GRACE_MS = 500;
const uint64_t US_PER_MS = 1000ULL;

bool modemSleepSupported = false;
bool modemIsSleeping = false;
unsigned long lastModemActivityTime = 0;
bool sensorWakeEnabledForLastSleep = false;
bool sensorWakeWaitingForRelease = false;

void noteModemActivity() {
  lastModemActivityTime = millis();
}

void setupPowerManagement() {
  // Wi-Fi, Bluetooth and GNSS are not used by this alarm. GNSS is never enabled;
  // explicitly stop the two ESP32 radios in case a core configuration started them.
  WiFi.mode(WIFI_OFF);
#if defined(CONFIG_BT_ENABLED) && CONFIG_BT_ENABLED
  btStop();
#endif

  if (setCpuFrequencyMhz(POWER_SAVING_CPU_MHZ)) {
    Serial.printf("[POWER] CPU frequency: %lu MHz\n",
                  (unsigned long)getCpuFrequencyMhz());
  } else {
    Serial.println("[POWER] CPU frequency reduction failed");
  }

  pinMode(MODEM_DTR_PIN, OUTPUT);
  digitalWrite(MODEM_DTR_PIN, LOW);  // DTR LOW keeps the A7670 awake
  pinMode(MODEM_RING_PIN, INPUT);    // A7670 RI is active LOW
  noteModemActivity();
}

void modemWake() {
  noteModemActivity();
  if (!modemIsSleeping) return;

  Serial.println("[POWER] Modem wake");
  digitalWrite(MODEM_DTR_PIN, LOW);
  delay(MODEM_WAKE_DELAY_MS);
  modemIsSleeping = false;
  noteModemActivity();
}

void enableModemSleep() {
  modemWake();
  modemSleepSupported = sendATCommand("AT+CSCLK=1", "OK", 5000);
  if (!modemSleepSupported) {
    Serial.println("[POWER] A7670 sleep mode could not be enabled");
  }
}

void modemSleep() {
  if (!modemSleepSupported || modemIsSleeping) return;

  Serial.println("[POWER] Modem sleep");
  digitalWrite(MODEM_DTR_PIN, HIGH);
  modemIsSleeping = true;
}

uint32_t nextWakeDelayMs() {
  uint32_t delayMs = batteryTimeUntilCheckMs();
  uint32_t sensorDelayMs = sensorTimeUntilDeadlineMs();
  if (sensorDelayMs < delayMs) delayMs = sensorDelayMs;
  return delayMs == 0 ? 1 : delayMs;
}

bool modemIdle() {
  return millis() - lastModemActivityTime >= MODEM_IDLE_GRACE_MS;
}

void handleWakeupReason() {
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("[POWER] ESP32 wake: TIMER");
    return;
  }

  if (cause == ESP_SLEEP_WAKEUP_GPIO) {
    bool modemRiActive = digitalRead(MODEM_RING_PIN) == LOW;

    // The classic ESP32 API reports GPIO as a group rather than identifying the pin.
    // RI is active LOW; any other configured GPIO wake is therefore the SW-420.
    if (modemRiActive) {
      Serial.println("[POWER] ESP32 wake: MODEM_RI");
      modemWake();
      // The RI pulse wakes the CPU before +CMTI is consumed. Process the UART buffer
      // immediately; do not scan all unread storage, which could replay old commands.
      pumpModemSerial();
    }
    if (!modemRiActive && sensorWakeEnabledForLastSleep) {
      Serial.println(sensorWakeWaitingForRelease
                       ? "[POWER] ESP32 wake: SENSOR_RELEASE"
                       : "[POWER] ESP32 wake: SENSOR");
    }
    return;
  }

  Serial.printf("[POWER] ESP32 wake: cause %d\n", (int)cause);
}

void configureGpioWakeup() {
  gpio_wakeup_disable((gpio_num_t)MODEM_RING_PIN);
  gpio_wakeup_disable((gpio_num_t)SENSOR_PIN);

  // A7670 RI idles HIGH and asserts LOW for an incoming SMS/call event.
  gpio_wakeup_enable((gpio_num_t)MODEM_RING_PIN, GPIO_INTR_LOW_LEVEL);

  sensorWakeEnabledForLastSleep = armed && !alarmTriggered;
  sensorWakeWaitingForRelease = false;
  if (sensorWakeEnabledForLastSleep) {
    // Normally wake on a new HIGH. If the SW-420 is still HIGH after a confirmed
    // event, wait efficiently for LOW instead of entering an immediate wake loop.
    sensorWakeWaitingForRelease = digitalRead(SENSOR_PIN) == HIGH;
    gpio_int_type_t sensorWakeLevel = sensorWakeWaitingForRelease
                                        ? GPIO_INTR_LOW_LEVEL
                                        : GPIO_INTR_HIGH_LEVEL;
    gpio_wakeup_enable((gpio_num_t)SENSOR_PIN, sensorWakeLevel);
  }

  esp_sleep_enable_gpio_wakeup();
}

void enterLightSleep() {
  // Keep processing while confirmation is pending, RI is asserted, serial data is
  // queued, or the modem is inside its short post-transaction response window.
  if (!sensorCanEnterLightSleep() ||
      digitalRead(MODEM_RING_PIN) == LOW ||
      Serial.available() || SerialAT.available() || !modemIdle()) {
    delay(1);
    return;
  }

  configureGpioWakeup();

  // Level wake-up can become active between the earlier idle check and sleep entry.
  // Defer sleeping so sensorLoop() can process that transition instead of asking
  // ESP-IDF to sleep with an already-active source (ESP_ERR_INVALID_STATE).
  bool sensorWakeAlreadyActive = sensorWakeEnabledForLastSleep &&
    (sensorWakeWaitingForRelease
       ? digitalRead(SENSOR_PIN) == LOW
       : digitalRead(SENSOR_PIN) == HIGH);
  if (digitalRead(MODEM_RING_PIN) == LOW || sensorWakeAlreadyActive) {
    delay(1);
    return;
  }

  // Calling esp_sleep_enable_timer_wakeup() again replaces the previous timeout.
  // No prior disable is needed (and disabling an unconfigured timer logs an error).
  esp_sleep_enable_timer_wakeup((uint64_t)nextWakeDelayMs() * US_PER_MS);

  modemSleep();
  Serial.println("[POWER] ESP32 entering light sleep");
  Serial.flush();

  esp_err_t result = esp_light_sleep_start();
  if (result != ESP_OK) {
    Serial.printf("[POWER] Light sleep failed: %d\n", (int)result);
    modemWake();
    delay(10);
    return;
  }

  handleWakeupReason();
}
