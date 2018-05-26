#ifndef __ESPZWAppliance_H__
#define __ESPZWAppliance_H__

#include <sys/time.h>
#include <FS.h>

#include <functional>

#include <user_interface.h>

#include <LinkedList.h>
#include <Timezone.h>
#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

#include "AppBaseUtils.hpp"

Dir Appliance_GetDir(String const &path);
time_t Appliance_CurrentTS();
time_t Appliance_UTCTimeofDay(struct tm *tm_out = nullptr);
time_t Appliance_LocalTimeofDay(struct tm *tm_out = nullptr,
	TimeChangeRule **tcr = nullptr);

JsonManagerResults Appliance_LoadConfig(String const &filename,
	std::function<void(JsonObject const &obj)> const &callback);
JsonManagerResults Appliance_UpdateConfig(String const &filename,
	JsonObjectCallback const &callback);

bool Appliance_RTCMemory_isRestored();
uint8_t Appliance_RTCMemory_Available();
bool Appliance_RTCMemory_Read(uint8_t offset, uint32_t *buf, uint8_t count);
bool Appliance_RTCMemory_Write(uint8_t offset, uint32_t *buf, uint8_t count);

AsyncWebServer* Appliance_WebPortal();
void Appliance_WebPortal_TimedStart();
void Appliance_WebPortal_Stop();
void Appliance_WebPortal_RespondBuiltInData(AsyncWebRequest &request,
	PGM_P data, String const &filename, int code = 200);
void Appliance_WebPortal_RespondFileOrBuiltIn(AsyncWebRequest &request,
	String const &filename, PGM_P defdata, int code = 200);
File Appliance_WebPortal_CheckRestoreRes(Dir &dir, String const &resfile,
	PGM_P resdata);

void Appliance_Service_Reload();
void Appliance_Device_Restart();

typedef enum {
	AP_PHY_11b = 0x1,
	AP_PHY_11g = 0x2,
	AP_PHY_11n = 0x4,
	AP_WPS     = 0x8
} APFeature;

struct APEntry {
	String SSID;
	uint8_t MAC[6];
	uint8_t Channel;
	int8_t RSSI;
	uint8_t Features;
	AUTH_MODE Auth;
};

typedef LinkedList<APEntry> TAPList;

#define APSCAN_FRESH_DURATION_DEFAULT 300

bool Appliance_APScan_Start(wifi_scan_type_t ScanType,
	time_t FreshDur = APSCAN_FRESH_DURATION_DEFAULT);
bool Appliance_APScan_InProgress();

void Appliance_EnumAPList(TAPList::Predicate const &pred);
void Appliance_ClearAPList();

#ifndef __ESPZWAppliance_Internal__

#define setup __userapp_setup
#define prestart_loop __userapp_prestart_loop
#define startup __userapp_startup
#define teardown __userapp_teardown
#define loop __userapp_loop

#endif //__ESPZWAppliance_Internal__

#endif //__ESPZWAppliance_H__