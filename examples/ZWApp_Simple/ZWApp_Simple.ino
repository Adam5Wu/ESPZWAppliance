// This demo requires a modified ESP8266 Arduino, found here:
// https://github.com/Adam5Wu/Arduino
// This demo requires the following libraries:
// https://github.com/Adam5Wu/ZWUtils-Arduino
// https://github.com/Adam5Wu/ESPVFATFS
// https://github.com/Adam5Wu/ArduinoJson
// https://github.com/Adam5Wu/Time
// https://github.com/Adam5Wu/Timezone
// https://github.com/Adam5Wu/ESPEasyAuth
// https://github.com/Adam5Wu/ESPAsyncTCP
// https://github.com/me-no-dev/ESPAsyncUDP
// https://github.com/Adam5Wu/ESPAsyncWebServer

// This demo presents a simplified bare-bone behaviour of the ZWAppliance
// All logic is built into the library
#define NO_GLOBAL_SPIFFS

#include <ESPZWAppliance.h>

void setup() {
  // This function is NOT the actual sketch setup.
  // (The sketch setup has been managed by the ZWAppliance)
  // This function is invoked once at the end of ZWAppliance setup.
  // The following has already been configured:
  // - Serial: 115200
  // - vFATFS: partition 0 mounted
  // - WiFi: non-presistent, 802.11n, light-sleep, max-power
  // - Time: Initialized to UTC 00:00:00 January 1, 2018, or
  //         approximately current time if time was synced before restart
  // - Timezone: loaded from configuration, or default UTC

	Serial.println("Service Setup!");
}

void loop() {
  // This function is NOT the actual sketch loop.
  // (The sketch loop has been managed by the ZWAppliance)
  // While the ZWAppliance is in service mode, this function is invoked
  //   in similar manner as sketch loop;
  // However, if the ZWAppliance is not in service mode, this function
  //   will not be invoked.

	Serial.println("Service Loop!");
	delay(1000);
}
