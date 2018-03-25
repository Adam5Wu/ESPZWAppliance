#ifndef __ESPZWAppliance_H__
#define __ESPZWAppliance_H__

#include <sys/time.h>
#include <FS.h>

#include <functional>

#include <ArduinoJson.h>
#include <ESPAsyncWebServer.h>

Dir Appliance_GetDir(String const &path);

void Appliance_LoadConfig(String const &filename,
	std::function<void(JsonObject &obj)> const &callback);

AsyncWebServer* Appliance_WebPortal();
void Appliance_WebPortal_TimedStart();
void Appliance_WebPortal_Stop();
void Appliance_WebPortal_RespondBuiltInData(AsyncWebRequest &request,
	PGM_P data, String const &filename, int code = 200);
void Appliance_WebPortal_RespondFileOrBuiltIn(AsyncWebRequest &request,
	String const &filename, PGM_P defdata, int code = 200);
File Appliance_WebPortal_CheckRestoreRes(Dir &dir, String const &resfile,
	PGM_P resdata);

void Appliance_Device_Restart();

#define setup __userapp_setup
#define startup __userapp_startup
#define teardown __userapp_teardown
#define loop __userapp_loop

#endif //__ESPZWAppliance_H__