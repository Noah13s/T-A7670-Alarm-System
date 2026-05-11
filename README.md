# T-A7670-Alarm-System
LTE-connected alarm system built around the SIMCom A7670 modem for remote monitoring, alerts, and sensor control.


| Name                                 | Value                                |
| ------------------------------------ | ------------------------------------ |
| Board                                | **ESP32 Dev Module**                 |
| Port                                 | Your port                            |
| CPU Frequency                        | 240MHZ(WiFi/BT)                      |
| Core Debug Level                     | None                                 |
| Erase All Flash Before Sketch Upload | Disable                              |
| Events Run On                        | Core1                                |
| Flash Frequency                      | 80MHZ                                |
| Flash Mode                           | QIO                                  |
| Flash Size                           | **4MB(32Mb)**                        |
| JTAG Adapter                         | Disabled                             |
| Arduino Runs On                      | Core1                                |
| Partition Scheme                     | **Huge APP (3MB No OTA/1MB SPIFFS)** |
| PSRAM                                | **Enable**                           |
| Upload Speed                         | 921600                               |
| Programmer                           | **Esptool**                          |

## Bill of Materials

| Component                     | Quantity | Unit Price (€) | Total (€) | Notes                             |
|------------------------------|----------|----------------|-----------|------------------------------------|
| Microcontroller              | 1        | 47.47          | 47.47     | Pre-soldered T-A7670E              |
| Vibration sensor             | 1        | 0.70           | 0.70      | SW-420                             |
| DC-DC Step Up                | 1        | 1.43           | 1.43      | XL6019                             |
| SIM Card                     | 1        | 2 per month    | 12        | Unlimited SMS plan                 |
| 120db Alarm                  | 1        | 3.09           | 3.09      | QSI-4840                           |
| Battery (18650)              | 1        | 1.75           | 1.75      | DMEGC INR18650-32E 3200mAh - 10A   |
| Misc (wires, screws)         | 1        | 2.46           | 2.46      | Approximation                      |
| **Total**                    |          |                | **77.32** |                                    |

```mermaid
stateDiagram-v2
    [*] --> Disarmed

    Disarmed --> ArmingDelay : Arm
    ArmingDelay --> Armed : Delay complete
    ArmingDelay --> Disarmed : Disarm

    state Armed {

        [*] --> Monitoring

        Monitoring --> Warning : Minor vibration
        Monitoring --> Triggered : Strong/repeated vibration

        Warning --> Monitoring : Activity stops
        Warning --> Triggered : Continued vibration

        Triggered --> Cooldown : Alarm timeout
        Cooldown --> Monitoring : Cooldown complete

        Monitoring --> AutoDisarm : Ignition ON
        Warning --> AutoDisarm : Ignition ON

        AutoDisarm --> [*]
    }

    Armed --> Disarmed : Auto disarm
    Armed --> Disarmed : Manual disarm

    Disarmed --> StolenMode : Enable stolen mode

    state StolenMode {

        [*] --> StolenMonitoring

        StolenMonitoring --> WarningStolen : Minor vibration
        StolenMonitoring --> TriggeredStolen : Ignition ON
        StolenMonitoring --> TriggeredStolen : Strong/repeated vibration

        WarningStolen --> StolenMonitoring : Activity stops
        WarningStolen --> TriggeredStolen : Continued vibration

        TriggeredStolen --> StolenCooldown : Alarm timeout
        StolenCooldown --> StolenMonitoring : Cooldown complete
    }

    StolenMode --> Disarmed : Manual disarm
```
