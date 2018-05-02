#ifndef __ESPZWAppliance_H__
#define __ESPZWAppliance_H__

#include <sys/time.h>
#include <FS.h>

#include <functional>

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

#define setup __userapp_setup
#define prestart_loop __userapp_prestart_loop
#define startup __userapp_startup
#define teardown __userapp_teardown
#define loop __userapp_loop

#endif //__ESPZWAppliance_H__