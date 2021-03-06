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

// This demo presents a bare-bone behaviour of the ZWAppliance
// It exposes more callback functions than ZWApp_Simple
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

void prestart_loop() {
  // This function works similar to loop() except:
  // - It is invoked when ZWAppliance is NOT in service mode
  //   * Current mode varies, can be one of APP_STARTUP, APP_INIT, APP_PORTAL
  // - Invocation interval varies, depending on appliance mode and states
  //   * When stable in APP_PORTAL mode, interval tend to match normal loop()
  //   * Otherwise, expect random interval up to ~100ms

	Serial.println("Service Pre-start Loopt!");
	delay(100);
}

bool startup() {
  // This function is invoked each time ZWAppliance enters service mode.
  // The following condition(s) has been reached:
  // - WiFi: Connected to configured access point
  // - Time: Synchronized with NTP server if configured (default not)
  // - WebPortal: Started if idle timeout configured (default 5min)
  // If return is false, appliance enters service bypass mode, and would
  //   not call startup/loop/teardown anymore (until next restart)

	Serial.println("Service Startup!");
	return true;
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

void teardown() {
  // This function is invoked each time ZWAppliance leaves service mode.
  // The following condition(s) has been reached:
  // - WiFi: The configure access point has been disconnected, and
  //           is not reachable within configured timeout

	Serial.println("Service Teardown!");
}
