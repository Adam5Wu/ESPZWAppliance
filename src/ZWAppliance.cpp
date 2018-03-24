#include <sys/time.h>
#include <limits.h>

#include <FS.h>
#include <sntp.h>
#include <user_interface.h>

#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

#include <ArduinoJson.h>

#include <Units.h>
#include <Timezone.h>
#include <vfatfs_api.h>
#include <ESPAsyncUDP.h>
#include <ESPEasyAuth.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJsonResponse.h>

#include "AppBaseUtils.hpp"
#include "PGMRES.hpp"

#define WLAN_PORTAL_IP      IPAddress(192, 168, 168, 168)
// Enable gateway for OS portal detection
#define WLAN_PORTAL_GATEWAY WLAN_PORTAL_IP
#define WLAN_PORTAL_SUBNET  IPAddress(255, 255, 255, 0)

#ifndef WLAN_PORTAL_GATEWAY
#define WLAN_PORTAL_GATEWAY IPAddress(0, 0, 0, 0)
#endif

#define PORTAL_SHUTDOWN_DELAY   1

#define CONFIG_DEFAULT_HOSTNAME_PFX       "ESP8266-"
#define CONFIG_DEFAULT_INIT_RETRY_CYCLE   10          // Seconds to wait before retry init steps (AP/NTP) or access point test
#define CONFIG_DEFAULT_INIT_RETRY_COUNT   6           // Number of retries before fallback to portal mode from init mode
#define CONFIG_DEFAULT_PORTAL_TIMEOUT     (5 * 60)    // Seconds the web portal is active and idle after enter service mode
#define CONFIG_DEFAULT_PORTAL_APTEST      30          // Seconds to test access point after entering portal mode and web portal is idle

enum AppState {
	APP_STARTUP = 0,
	APP_INIT,
	APP_PORTAL,
	APP_SERVICE,
	APP_DEVRESET,
	APP_DEVRESTART,
};

ESPAPP_DEBUGDO(
PGM_P StrAppState(AppState state) {
	switch (state) {
		case APP_STARTUP: return PSTR_C("Startup");
		case APP_INIT: return PSTR_C("Init");
		case APP_PORTAL: return PSTR_C("Portal");
		case APP_SERVICE: return PSTR_C("Service");
		case APP_DEVRESET: return PSTR_C("Device-Reset");
		case APP_DEVRESTART: return PSTR_C("Device-Restart");
		default: return PSTR_C("<Unknown>");
	}
}
)

enum InitSteps {
	INIT_STA_CONNECT = 0,
	INIT_NTP_SETUP,
	INIT_FAIL
};

enum PortalSteps {
	PORTAL_OFF = 0,
	PORTAL_DOWN,
	PORTAL_SETUP,
	PORTAL_ACCOUNT,
	PORTAL_ACCESSCTRL,
	PORTAL_FILES,
	PORTAL_START,
	PORTAL_UP,
	PORTAL_DEVRESET,
	PORTAL_DEVRESTART,
};

struct {
	AppState State;
	time_t StageTS;
	time_t StartTS;

	AsyncWebServer* webServer;
	HTTPDigestAccountAuthority* webAccounts;
	SessionAuthority* webAuthSessions;
	time_t wsActivityTS;
	PortalSteps wsSteps;

	union {
		struct {
			time_t cycleTS;
			time_t lastKnownTS;
			ESPAPP_DEBUGVVDO(time_t cycleSpan);
			InitSteps steps;
			uint8_t cycleCount;
		} init;
		struct {
			Ticker* apTestTimer;
			AsyncUDP* dnsServer;
			bool performAPTest;
		} portal;
		struct {
			Ticker* portalTimer;
			Ticker* apTestTimer;
			time_t lastAPAvailableTS;
			bool portalFallback;
		} service;
	};
} AppGlobal;

time_t GetCurrentTS() {
	struct timeval TV;
	gettimeofday(&TV, nullptr);
	return TV.tv_sec;
}

struct {
	String WLAN_AP_Name;
	String WLAN_AP_Pass;
	unsigned int Init_Retry_Count;
	unsigned int Init_Retry_Cycle;

	String Hostname;
	unsigned int Portal_APTest;
	unsigned int Portal_Timeout;

	String NTP_Server;
	Timezone TZ;
} AppConfig;

String PrintTime(time_t ts) {
	String StrTime('\0', 25);
	TimeChangeRule* TZ;
	time_t LocTime = AppConfig.TZ.toLocal(ts, &TZ);
	ctime_r(&LocTime, StrTime.begin());
	StrTime.end()[-1] = ' ';
	StrTime.concat(TZ->abbrev);
	return std::move(StrTime);
}

void Portal_Stop();

extern void __userapp_setup();
extern void __userapp_startup();
extern void __userapp_loop();
extern void __userapp_teardown();

time_t FinalizeCurrentState() {
	switch (AppGlobal.State) {
		case APP_STARTUP:
			// Do Nothing
			break;

		case APP_INIT:
			// Do Nothing
			break;

		case APP_PORTAL:
			delete AppGlobal.portal.apTestTimer;
			delete AppGlobal.portal.dnsServer;
			break;

		case APP_SERVICE:
			delete AppGlobal.service.portalTimer;
			delete AppGlobal.service.apTestTimer;
			__userapp_teardown();
			break;

		case APP_DEVRESET:
			// Do Nothing
			break;

		case APP_DEVRESTART:
			// Do Nothing
			break;

		default:
			ESPAPP_LOG("Unrecognized state (%d)\n", (int)AppGlobal.State);
			panic();
	}
	Portal_Stop();
	time_t curTS = GetCurrentTS();
	ESPAPP_DEBUG("End of state [%s] (%s)\n", SFPSTR(StrAppState(AppGlobal.State)),
		ToString(curTS - AppGlobal.StageTS, TimeUnit::SEC, true).c_str());
	time_t StartTS = AppGlobal.StartTS;
	memset(&AppGlobal, 0, sizeof(AppGlobal));
	AppGlobal.StartTS = StartTS;
	return curTS;
}

void SwitchState(AppState state) {
	AppGlobal.StageTS = FinalizeCurrentState();
	AppGlobal.State = state;
	ESPAPP_DEBUG("Start of state [%s] @%s\n", SFPSTR(StrAppState(AppGlobal.State)),
		PrintTime(AppGlobal.StageTS).c_str());
}

Dir get_dir(String const &path) {
	auto Ret = mkdirs(VFATFS, path);
	if (!Ret) {
		ESPAPP_LOG("ERROR: Unable to create directory '%s'\n", path.c_str());
		panic();
	}
	return Ret;
}

int8_t load_config_timezone(JsonObject &obj, TimeChangeRule &r) {
	if (obj.success()) {
		String Name = obj[FC("Name")] | String(FC("Local")).c_str();
		// The following parameters are mandatory!
		// Use invalid default value to simplify error handling
		uint8_t Week = obj[FC("Week")] | -1;
		uint8_t DoW = obj[FC("DayOfWeek")] | -1;
		uint8_t Month = obj[FC("Month")] | -1;
		uint8_t Hour = obj[FC("Hour")] | -1;
		int Offset = obj[FC("Offset")] | INT_MAX;
		// Handle Name
		if (Name.length() > 5) {
			ESPAPP_DEBUG("WARNING: Timezone rule name too long, truncated to '%s'\n",
				Name.substring(0, 5).c_str());
		}
		memcpy(r.abbrev, Name.c_str(), std::min(6u, Name.length() + 1));
		// Handle Week
		if (Week > 4) {
			ESPAPP_DEBUG("WARNING: Invalid timezone rule week, expect [0..4]\n");
			return 1;
		}
		r.week = Week;
		// Handle DoW
		if (DoW > 6) {
			ESPAPP_DEBUG("WARNING: Invalid timezone rule day-of-week, expect [0..6]\n");
			return 2;
		}
		r.dow = DoW;
		// Handle Month
		if (Month > 11) {
			ESPAPP_DEBUG("WARNING: Invalid timezone rule month, expect [0..11]\n");
			return 3;
		}
		r.month = Month;
		// Handle Hour
		if (Hour > 23) {
			ESPAPP_DEBUG("WARNING: Invalid timezone rule hour, expect [0..23]\n");
			return 4;
		}
		r.hour = Hour;
		// Handle Offset
		if (Offset < -12 * 60 || Offset > +14 * 60) {
			ESPAPP_DEBUG("WARNING: Invalid timezone rule offset, expect [-12*60..+14*60]\n");
			return 5;
		}
		r.offset = Offset;
		// When we reach here, all fields are parsed OK
		return 0;
	}
	return -2;
}

void load_config(String const &filename) {
	AppConfig.WLAN_AP_Name.clear();
	AppConfig.WLAN_AP_Pass.clear();
	AppConfig.Init_Retry_Count = CONFIG_DEFAULT_INIT_RETRY_COUNT;
	AppConfig.Init_Retry_Cycle = CONFIG_DEFAULT_INIT_RETRY_CYCLE;
	AppConfig.Hostname = String(FC(CONFIG_DEFAULT_HOSTNAME_PFX)) + String(ESP.getChipId(), 16);
	AppConfig.Portal_Timeout = CONFIG_DEFAULT_PORTAL_TIMEOUT;
	AppConfig.Portal_APTest = CONFIG_DEFAULT_PORTAL_APTEST;

	auto ConfigDir = get_dir(FC(CONFIG_DIR));
	if (JsonManager(ConfigDir, filename, true,
		[&](JsonObject & obj, BoundedDynamicJsonBuffer & parsebuf) {
			AppConfig.WLAN_AP_Name = obj[FC("WLAN_AP_Name")] | AppConfig.WLAN_AP_Name.c_str();
			AppConfig.WLAN_AP_Pass = obj[FC("WLAN_AP_Pass")] | AppConfig.WLAN_AP_Pass.c_str();
			AppConfig.Init_Retry_Count = obj[FC("Init_Retry_Count")] | AppConfig.Init_Retry_Count;
			AppConfig.Init_Retry_Cycle = obj[FC("Init_Retry_Cycle")] | AppConfig.Init_Retry_Cycle;
			AppConfig.Hostname = obj[FC("Hostname")] | AppConfig.Hostname.c_str();
			AppConfig.NTP_Server = obj[FC("NTP_Server")] | AppConfig.NTP_Server.c_str();
			AppConfig.Portal_Timeout = obj[FC("Portal_Timeout")] | AppConfig.Portal_Timeout;
			AppConfig.Portal_APTest = obj[FC("Portal_APTest")] | AppConfig.Portal_APTest;
			do {
				// Load timezone configurations
				TimeChangeRule TZR, TZD;
				if (load_config_timezone(obj[FC("TimeZone-Regular")].as<JsonObject&>(), TZR) != 0) {
					ESPAPP_LOG("WARNING: Failed to parse regular timezone configuration, "
						"use default timezone!\n");
					break;
				}
				if (load_config_timezone(obj[FC("TimeZone-Daylight")].as<JsonObject&>(), TZR) != 0) {
					if (!obj.containsKey(FC("TimeZone-Daylight"))) {
						ESPAPP_LOG("WARNING: Failed to parse daylight-saving timezone configuration, "
							"use regular timezone!\n");
					}
					TZD = TZR;
				}
				AppConfig.TZ.setRules(TZD, TZR);
			} while (false);
			return false;
		}, [&](File & file) {
			return file.truncate(0);
		}) >= JSONMAN_ERR) {
		ESPAPP_LOG("ERROR: Error processing configuration file '%s'\n", filename.c_str());
		panic();
	}
}

extern "C" {
	void tune_timeshift64 (uint64_t now_us);
}

void InitBootTime() {
	struct tm BootTM;
	BootTM.tm_year = 2018 - 1900;
	BootTM.tm_mon = 0;
	BootTM.tm_mday = 1;
	BootTM.tm_hour = 0;
	BootTM.tm_min = 0;
	BootTM.tm_sec = 0;
	tune_timeshift64(mktime(&BootTM) * 1000000ull);
}

void setup() {
	Serial.begin(115200);
	delay(100);
	Serial.println();

	// Set a reasonable start time
	InitBootTime();

	Serial.println(FC("ESP8266 ZWAppliance Core v" APP_VERSION));
	memset(&AppGlobal, 0, sizeof(AppGlobal));
	//AppGlobal.State = APP_STARTUP;
	AppGlobal.StartTS = GetCurrentTS();
	AppGlobal.StageTS = AppGlobal.StartTS;
	//AppGlobal.wsSteps = PORTAL_OFF;

	ESPAPP_DEBUG("Starting Filesystem...\n");
	// Two partitions, one primary, one backup
	VFATPartitions::config(50, 50);
	if (!VFATFS.begin()) {
		ESPAPP_DEBUG("ERROR: Failed to start file system!\n");
		panic();
	} else {
		FSInfo info;
		VFATFS.info(info);
		ESPAPP_LOG("FATFS: %s total, %s used (%.1f%%)\n",
			ToString(info.totalBytes, SizeUnit::BYTE, true).c_str(),
			ToString(info.usedBytes, SizeUnit::BYTE, true).c_str(),
			info.usedBytes * 100.0 / info.totalBytes);
	}

	ESPAPP_DEBUG("Loading Configurations...\n");
	load_config(APPCONFIG_FILE);

	ESPAPP_DEBUG("Initializing WiFi...\n");
	if (WiFi.getMode() != WIFI_OFF) {
		if (!WiFi.mode(WIFI_OFF)) {
			ESPAPP_LOG("WARNING: Failed to disable WiFi!\n");
		}
	}
	if (WiFi.getAutoConnect()) {
		WiFi.setAutoConnect(false);
	}
	// Order is important
	WiFi.persistent(false);

	if (WiFi.getSleepMode() != WIFI_LIGHT_SLEEP) {
		if (!WiFi.setSleepMode(WIFI_LIGHT_SLEEP)) {
			ESPAPP_LOG("WARNING: Failed to configure WiFi energy saving!\n");
		}
	}
	if (WiFi.getPhyMode() != WIFI_PHY_MODE_11N) {
		if (!WiFi.setPhyMode(WIFI_PHY_MODE_11N)) {
			ESPAPP_LOG("WARNING: Failed to configure WiFi in 802.11n mode!\n");
		}
	}
	WiFi.setOutputPower(20.5);

	__userapp_setup();
}

void Init_WLAN_Connect() {
	AppGlobal.init.cycleTS = GetCurrentTS();
	if (WiFi.begin(AppConfig.WLAN_AP_Name.c_str(), AppConfig.WLAN_AP_Pass.c_str())
		== WL_CONNECT_FAILED) {
		ESPAPP_LOG("WARNING: Failed to initialize WiFi client!\n");
	}
	delay(200);
}

void Init_Start() {
	SwitchState(APP_INIT);
	ESPAPP_LOG("Connecting to WiFi station '%s'...\n",
		AppConfig.WLAN_AP_Name.c_str());
	if (!WiFi.mode(WIFI_STA)) {
		ESPAPP_LOG("ERROR: Failed to switch to WiFi client mode!\n");
		panic();
	}
	if (WiFi.getAutoReconnect()) {
		WiFi.setAutoReconnect(false);
	}
	auto hostname = WiFi.hostname();
	if (hostname == AppConfig.Hostname) {
		WiFi.hostname(AppConfig.Hostname.c_str());
	}

	Init_WLAN_Connect();
	//AppGlobal.init.steps = INIT_STA_CONNECT;
}

void Portal_Start();
void Service_Start();

void Init_NTP_Sync() {
	AppGlobal.init.cycleTS = AppGlobal.init.lastKnownTS = GetCurrentTS();
	configTime(0, 0, AppConfig.NTP_Server.c_str());
	delay(200);
}

void Init_NTP() {
	AppGlobal.init.steps = INIT_NTP_SETUP;
	if (AppConfig.NTP_Server) {
		ESPAPP_DEBUG("Synchronizing with NTP server '%s'...\n",
			AppConfig.NTP_Server.c_str());
		Init_NTP_Sync();
	}
}

void Init_TimeoutTrigger() {
	ESPAPP_DEBUGV("WiFi network initialization did not complete in time\n");
	AppGlobal.init.steps = INIT_FAIL;
}

void loop_INIT() {
	switch (AppGlobal.init.steps) {
		case INIT_STA_CONNECT: {
			station_status_t WifiStatus = wifi_station_get_connect_status();
			switch (WifiStatus) {
				case STATION_WRONG_PASSWORD:
					ESPAPP_DEBUG("WARNING: Wrong credential for WiFi access point '%s'\n",
						AppConfig.WLAN_AP_Name.c_str());
					AppGlobal.init.steps = INIT_FAIL;
					break;

				case STATION_GOT_IP:
					WiFi.setAutoReconnect(true);
					ESPAPP_DEBUG("Connected to WiFi access point '%s'\n",
						AppConfig.WLAN_AP_Name.c_str());
					ESPAPP_LOG("IP address: %s\n", WiFi.localIP().toString().c_str());
					ESPAPP_DEBUG("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
					ESPAPP_DEBUG("Name Server: %s\n", WiFi.dnsIP().toString().c_str());
					Init_NTP();
					break;

				default: {
					time_t ConnectSpan = GetCurrentTS() - AppGlobal.init.cycleTS;
					if (ConnectSpan < AppConfig.Init_Retry_Cycle) {
						ESPAPP_DEBUGVVDO({
							time_t cycleSpan = AppConfig.Init_Retry_Cycle - ConnectSpan;
							if (cycleSpan != AppGlobal.init.cycleSpan) {
								AppGlobal.init.cycleSpan = cycleSpan;
								ESPAPP_DEBUG_LOG("Waiting for WiFi connection... "
									"(%s to next retry)\n",
									ToString(cycleSpan, TimeUnit::SEC, true).c_str());
							}
						});
						delay(100);
						break;
					}
					if (++AppGlobal.init.cycleCount < AppConfig.Init_Retry_Count) {
						WiFi.disconnect(false);
						ESPAPP_DEBUGV("WiFi access point connect timeout, retrying... "
							"(%d retries left)\n",
							AppConfig.Init_Retry_Count - AppGlobal.init.cycleCount);
						delay(100);
						Init_WLAN_Connect();
					} else Init_TimeoutTrigger();
				}
			}
		} break;

		case INIT_NTP_SETUP: {
			if (AppConfig.NTP_Server) {
				time_t curTS = sntp_get_current_timestamp();
				if (curTS < AppGlobal.init.lastKnownTS) {
					AppGlobal.init.lastKnownTS = GetCurrentTS();
					time_t ConnectSpan = AppGlobal.init.lastKnownTS - AppGlobal.init.cycleTS;
					if (ConnectSpan < AppConfig.Init_Retry_Cycle) {
						ESPAPP_DEBUGVVDO({
							time_t cycleSpan = AppConfig.Init_Retry_Cycle - ConnectSpan;
							if (cycleSpan != AppGlobal.init.cycleSpan) {
								AppGlobal.init.cycleSpan = cycleSpan;
								ESPAPP_DEBUG_LOG("Waiting for NTP synchronization... "
									"(%s to next retry)\n",
									ToString(cycleSpan, TimeUnit::SEC, true).c_str());
							}
						});
						delay(100);
						break;
					}
					if (++AppGlobal.init.cycleCount < AppConfig.Init_Retry_Count) {
						ESPAPP_DEBUGV("NTP synchronization timeout, retrying... "
							"(%d retries left)\n",
							AppConfig.Init_Retry_Count - AppGlobal.init.cycleCount);
						Init_NTP_Sync();
					} else Init_TimeoutTrigger();
					break;
				}
				ESPAPP_LOG("NTP time synchronized @%s\n", PrintTime(curTS).c_str());
				// Update the stage timestamp
				time_t StageTime = AppGlobal.init.lastKnownTS - AppGlobal.StageTS;
				AppGlobal.StageTS = curTS - StageTime;
				// Update the boot timestamp
				time_t UpTime = AppGlobal.init.lastKnownTS - AppGlobal.StartTS;
				ESPAPP_DEBUG("Uptime: %s\n", ToString(UpTime, TimeUnit::SEC, true).c_str());
				AppGlobal.StartTS = curTS - UpTime;
			} else {
				ESPAPP_DEBUG("NTP server not configured, synchronization skipped\n");
			}
			Service_Start();
		} break;

		case INIT_FAIL: {
			ESPAPP_LOG("WiFi network initialization failed!\n");
			WiFi.disconnect(true);
			delay(100);
			Portal_Start();
		} break;

		default: {
			ESPAPP_LOG("ERROR: Unrecognized init step (%d)\n",
				(int)AppGlobal.init.steps);
			panic();
		}
	}
}

#include "API.Config.hpp"
#include "API.OTA.hpp"

void Portal_WebServer_RespondBuiltInData(AsyncWebRequest &request,
	PGM_P data, String const &filename, int code = 200) {
	ESPAPP_DEBUG("* Serving built-in data for '%s'...\n", filename.c_str());
	request.send_P(code, data, AsyncFileResponse::contentTypeByName(filename));
}

void Portal_WebServer_RespondFileOrBuiltIn(AsyncWebRequest &request,
	String const &filename, PGM_P defdata, int code = 200) {
	auto PortalDir = get_dir(FC(PORTAL_DIR));
	File PageData = PortalDir.openFile(filename, "r");
	if (PageData) {
		request.send(PageData, filename, String::EMPTY, code);
	} else {
		Portal_WebServer_RespondBuiltInData(request, defdata, filename, code);
	}
}

File Portal_WebServer_CheckRestoreRes(Dir &dir, String const &resfile, PGM_P resdata) {
	File ResFile = dir.openFile(resfile, "r");
	if (!ResFile) {
		ESPAPP_DEBUG("* Restoring portal resource file '%s'...\n", resfile.c_str());
		ResFile = dir.openFile(resfile, "w+");
		if (ResFile) {
			StreamString ResData(FPSTR(resdata));
			size_t bufofs = 0;
			while (ResData.length() > bufofs) {
				size_t outlen = ResFile.write(((uint8_t*)ResData.begin()) + bufofs,
					ResData.length() - bufofs);
				if (outlen < 0) {
					ESPAPP_DEBUG("WARNING: failed to write portal resource file '%s'\n",
						resfile.c_str());
					ResFile.remove();
					break;
				}
				bufofs += outlen;
			}
			if (!ResFile.seek(0)) {
				// Should not reach
				ESPAPP_DEBUG("WARNING: Failed to seek resource file '%s'\n",
					resfile.c_str());
				ResFile = File();
			}
		} else {
			ESPAPP_DEBUG("WARNING: Unable to create portal resource file '%s'\n",
				resfile.c_str());
		}
	}
	return std::move(ResFile);
}

void Portal_WebServer_Operations() {
	switch (AppGlobal.wsSteps) {
		case PORTAL_OFF:
		case PORTAL_UP: {
			// Do Nothing
		} break;

		case PORTAL_DOWN: {
			Portal_Stop();
		} break;

		case PORTAL_SETUP: {
			AppGlobal.webServer = new AsyncWebServer(80);
			AppGlobal.webServer->configRealm(AppConfig.Hostname);
			AppGlobal.wsSteps = PORTAL_ACCOUNT;
		} break;

		case PORTAL_ACCOUNT: {
			auto ConfigDir = get_dir(FC(CONFIG_DIR));
			ESPAPP_DEBUG("Loading portal accounts...\n");

			AppGlobal.webAccounts = new HTTPDigestAccountAuthority(AppConfig.Hostname);
			File AccountData = ConfigDir.openFile(FC(PORTAL_ACCOUNTS_FILE), "r");
			if (AccountData) {
				int AccountCnt = AppGlobal.webAccounts->loadAccounts(AccountData);
				ESPAPP_DEBUG("* Loaded %d accounts.\n", AccountCnt);
				if (AppGlobal.webAccounts->getIdentity(FC(PORTAL_ADMIN_USER)) == IdentityProvider::UNKNOWN) {
					ESPAPP_DEBUG("WARNING: default administrator account '%s' not found, "
						"re-initializing...\n",
						SFPSTR(FC(PORTAL_ADMIN_USER)));
					AppGlobal.webAccounts->addAccount(FC(PORTAL_ADMIN_USER), FC(PORTAL_ADMIN_USER));
				}
			} else {
				ESPAPP_DEBUG("WARNING: accounts file '%s' not found, using built-in accounts...\n",
					SFPSTR(FC(PORTAL_ACCOUNTS_FILE)));
				AppGlobal.webAccounts->addAccount(FC(PORTAL_ADMIN_USER), FC(PORTAL_ADMIN_USER));
			}
			AppGlobal.webAuthSessions = new SessionAuthority(AppGlobal.webAccounts,
				AppGlobal.webAccounts);
			AppGlobal.wsSteps = PORTAL_ACCESSCTRL;
		} break;

		case PORTAL_ACCESSCTRL: {
			auto ConfigDir = get_dir(FC(CONFIG_DIR));
			ESPAPP_DEBUG("Loading access control...\n");
			{
				File ACLData = Portal_WebServer_CheckRestoreRes(ConfigDir,
					FC(PORTAL_ACCESS_FILE), PORTAL_DEFAULT_ACL);
				if (ACLData) {
					AppGlobal.webServer->configAuthority(*AppGlobal.webAuthSessions, ACLData);
				} else {
					StreamString ACLData(FPSTR(PORTAL_DEFAULT_ACL));
					AppGlobal.webServer->configAuthority(*AppGlobal.webAuthSessions, ACLData);
				}
			}

			AuthSession fakeAnonyAuth(IdentityProvider::ANONYMOUS, nullptr);
			if (AppGlobal.webServer->_checkACL(HTTP_GET, FC(PORTAL_ROOT), &fakeAnonyAuth) != ACL_ALLOWED) {
				ESPAPP_DEBUG("WARNING: correcting ACL to allow '%s' access to portal root...\n",
					SFPSTR(FC(ANONYMOUS_ID)));
				AppGlobal.webServer->_prependACL(FC(PORTAL_ROOT), HTTP_BASIC_READ,
					{nullptr, {&IdentityProvider::ANONYMOUS}});
			}
			{
				auto &AdminIdent = AppGlobal.webAccounts->getIdentity(FC(PORTAL_ADMIN_USER));
				AuthSession fakeAdminAuth(AdminIdent, nullptr);
				if (AppGlobal.webServer->_checkACL(HTTP_PUT, FC(PORTAL_FSDAV_ROOT), &fakeAdminAuth) != ACL_ALLOWED ||
					AppGlobal.webServer->_checkACL(HTTP_PUT, FC(PORTAL_FSDAV_ROOT), &fakeAnonyAuth) == ACL_ALLOWED) {
					ESPAPP_DEBUG("WARNING: correcting ACL to allow '%s' exclusive access to '%s'...\n",
						SFPSTR(FC(PORTAL_ADMIN_USER)), SFPSTR(FC(PORTAL_FSDAV_ROOT)));
					AppGlobal.webServer->_prependACL(FC(PORTAL_FSDAV_ROOT), HTTP_ANY,
						{nullptr, {&AdminIdent}});
				}
				if (AppGlobal.webServer->_checkACL(HTTP_GET, FC(PORTAL_API_HWCTL), &fakeAdminAuth) != ACL_ALLOWED ||
					AppGlobal.webServer->_checkACL(HTTP_GET, FC(PORTAL_API_HWCTL), &fakeAnonyAuth) == ACL_ALLOWED) {
					ESPAPP_DEBUG("WARNING: correcting ACL to allow '%s' exclusive access to '%s'...\n",
						SFPSTR(FC(PORTAL_ADMIN_USER)), SFPSTR(FC(PORTAL_API_HWCTL)));
					AppGlobal.webServer->_prependACL(FC(PORTAL_API_HWCTL), HTTP_BASIC_READ,
						{nullptr, {&AdminIdent}});
				}
			}
			AppGlobal.wsSteps = PORTAL_FILES;
		} break;

		case PORTAL_FILES: {
			auto PortalDir = get_dir(FC(PORTAL_DIR));

			Portal_WebServer_CheckRestoreRes(PortalDir, FC(PORTAL_OTARES_MD5JS),
				PORTAL_RESDATA_MD5JS);

			AppGlobal.wsSteps = PORTAL_START;
		} break;

		case PORTAL_START: {
			ESPAPP_DEBUG("Bringing up portal service...\n");
			bool isCaptive = AppGlobal.State == APP_PORTAL;

			if (isCaptive) {
				// Redirect all request to this host (and hence being captive)
				auto pHandler = new AsyncHostRedirWebHandler(AppConfig.Hostname, HTTP_ANY);
				pHandler->altPath = PORTAL_ROOT;
				pHandler->psvPaths.append(FC(PORTAL_WPAD_FILE));
				AppGlobal.webServer->addHandler(pHandler).addFilter(
					[&](AsyncWebRequest const & request) {
						// Record last activity time stamp
						AppGlobal.wsActivityTS = GetCurrentTS();
						return true;
					});
			} else {
				AppGlobal.webServer->addHandler(new AsyncPassthroughWebHandler()).addFilter(
					[&](AsyncWebRequest const & request) {
						// Record last activity time stamp
						AppGlobal.wsActivityTS = GetCurrentTS();
						return false;
					});
			}

			{
				auto &Handler = AppGlobal.webServer->on(FC(PORTAL_WPAD_FILE"$"),
					[](AsyncWebRequest & request) {
						request.send(404);
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FC(PORTAL_API_HWCTL_DEVRESET"$"),
					[](AsyncWebRequest & request) {
						Portal_WebServer_RespondFileOrBuiltIn(request,
							FC(PORTAL_DEVRESET_FILE), PORTAL_DEVRESET_PAGE);
						AppGlobal.wsSteps = PORTAL_DEVRESET;
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FC(PORTAL_API_HWCTL_DEVRESTART"$"),
					[](AsyncWebRequest & request) {
						Portal_WebServer_RespondFileOrBuiltIn(request,
							FC(PORTAL_DEVRESTART_FILE), PORTAL_DEVRESTART_PAGE);
						AppGlobal.wsSteps = PORTAL_DEVRESTART;
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FC(PORTAL_API_HWMON_HEAP"$"),
					[](AsyncWebRequest & request) {
						size_t FreeHeap = ESP.getFreeHeap();
						request.send(200, String(FreeHeap), FC("text/plain"));
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FC(PORTAL_API_HWMON_UPTIME"$"),
					[](AsyncWebRequest & request) {
						time_t UpTime = GetCurrentTS() - AppGlobal.StartTS;
						request.send(200, String(UpTime), FC("text/plain"));
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FC(PORTAL_API_HWMON),
					[](AsyncWebRequest & request) {
						size_t FreeHeap = ESP.getFreeHeap();
						time_t UpTime = GetCurrentTS() - AppGlobal.StartTS;
						AsyncJsonResponse * response =
							AsyncJsonResponse::CreateNewObjectResponse();
						response->root[FC("heap")] = FreeHeap;
						response->root[FC("uptime")] = UpTime;
						request.send(response);
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->addHandler(
					new AsyncAPIConfigWebHandler(FC(PORTAL_API_CONFIG),
						get_dir(FC(CONFIG_DIR)))
				);
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->addHandler(
					new AsyncAPIOTAWebHandler(FC(PORTAL_API_OTA),
						FC(PORTAL_ROOT PORTAL_OTA_FILE))
				);
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->serveStatic(FC(PORTAL_FSDAV_ROOT),
					VFATFS.openDir(FC("/")), String::EMPTY, FC(DEFAULT_CACHE_CTRL),
					true, true);
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->serveStatic(FC(PORTAL_ROOT),
					get_dir(FC(PORTAL_DIR)), FC(PORTAL_INDEX_FILE), FC(DEFAULT_CACHE_CTRL));
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const & request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}

				Handler._onGETIndexNotFound = [](AsyncWebRequest & request) {
					if (request.url() == FC(PORTAL_ROOT)) {
						Portal_WebServer_RespondBuiltInData(request,
							PORTAL_INDEX_PAGE, FC(PORTAL_INDEX_FILE));
					} else {
						// Do not allow directory listing
						request.send(403);
					}
				};

				Handler._onGETPathNotFound = [](AsyncWebRequest & request) {
					if (request.url() == FC(PORTAL_ROOT PORTAL_OTA_FILE)) {
						Portal_WebServer_RespondBuiltInData(request,
							PORTAL_OTA_PAGE, FC(PORTAL_OTA_FILE));
					} else if (request.url() == FC(PORTAL_ROOT PORTAL_OTARES_OTACOREJS)) {
						Portal_WebServer_RespondBuiltInData(request,
							PORTAL_RESDATA_OTACOREJS, FC(PORTAL_OTARES_OTACOREJS));
					} else {
						request.send(404);
					}
				};
			}

			AppGlobal.webServer->begin();
			AppGlobal.wsSteps = PORTAL_UP;
			AppGlobal.wsActivityTS = GetCurrentTS();
			ESPAPP_LOG("Device portal is running...\n");
		} break;

		case PORTAL_DEVRESET:
			delay(PORTAL_SHUTDOWN_DELAY * 1000);
			SwitchState(APP_DEVRESET);
			break;

		case PORTAL_DEVRESTART:
			delay(PORTAL_SHUTDOWN_DELAY * 1000);
			SwitchState(APP_DEVRESTART);
			break;

		default: {
			ESPAPP_LOG("ERROR: Unrecognized portal step (%d)\n",
				(int)AppGlobal.wsSteps);
			panic();
		}
	}
}

void Portal_APConfig() {
	if (!WiFi.softAPConfig(WLAN_PORTAL_IP, WLAN_PORTAL_GATEWAY, WLAN_PORTAL_SUBNET)) {
		ESPAPP_LOG("ERROR: Failed to configure WiFi base station!\n");
		panic();
	}
	if (!WiFi.softAP(AppConfig.Hostname.c_str())) {
		ESPAPP_LOG("ERROR: Failed to start WiFi base station!\n");
		panic();
	}
	ESPAPP_LOG("WiFi base station '%s' enabled!\n", AppConfig.Hostname.c_str());
}

void Portal_TimeAPTest() {
	unsigned int PortalIdle = GetCurrentTS() - AppGlobal.wsActivityTS;
	if (PortalIdle >= AppConfig.Portal_APTest) {
		AppGlobal.wsActivityTS = GetCurrentTS();
		if (AppConfig.WLAN_AP_Name) {
			ESPAPP_DEBUG("Portal idle for %s, testing WiFi access point...\n",
				ToString(PortalIdle, TimeUnit::SEC, true).c_str());
			AppGlobal.portal.performAPTest = true;
		}
	}
}

bool Portal_DNS_IsSingleQuery(DNSHeader *header) {
	return ntohs(header->QDCount) == 1 &&
		header->ANCount == 0 &&
		header->NSCount == 0 &&
		header->ARCount == 0;
}

String Portal_DNS_ParseQueryName(DNSHeader *header) {
	String queryName;
	unsigned char *start = ((unsigned char*)header) + 12;
	if (*start) {
		int pos = 0;
		while (true) {
			unsigned char labelLength = start[pos];
			for (int i = 0; i < labelLength; i++) {
				pos++;
				queryName += (char)start[pos];
			}
			pos++;
			if (start[pos]) {
				queryName += ".";
			} else break;
		}
	}
	return queryName;
}

void Portal_DNS_SendReply(DNSHeader *header, AsyncUDPPacket &packet) {
	AsyncUDPMessage reply(packet.length() + 16);

	header->QR = DNS_QR_RESPONSE;
	header->ANCount = header->QDCount;

	reply.write(packet.data(), packet.length());

	reply.write(192); // answer name is a pointer
	reply.write(12);  // pointer to offset at 0x00c

	reply.write(0);   // 0x0001 answer is type A query (host address)
	reply.write(1);

	reply.write(0);   // 0x0001 answer is class IN (internet address)
	reply.write(1);

	uint32_t _ttl = htonl(60);
	reply.write((const uint8_t *)&_ttl, 4);

	// Length of RData is 4 bytes (because, in this case, RData is IPv4)
	IPAddress portalIP = WLAN_PORTAL_IP;
	uint8_t _ip[4] = { portalIP[0], portalIP[1], portalIP[2], portalIP[3] };
	reply.write(0);
	reply.write(4);
	reply.write(_ip, 4);

	packet.send(reply);
}

void Portal_Start() {
	SwitchState(APP_PORTAL);
	if (!WiFi.mode(WIFI_AP)) {
		ESPAPP_LOG("ERROR: Failed to switch to WiFi base station mode!\n");
		panic();
	}
	Portal_APConfig();

	AppGlobal.portal.dnsServer = new AsyncUDP();
	if (AppGlobal.portal.dnsServer->listen(53)) {
		AppGlobal.portal.dnsServer->onPacket([](AsyncUDPPacket & packet) {
			if (packet.length() >= sizeof(DNSHeader)) {
				DNSHeader *header = (DNSHeader*)packet.data();
				if (header->QR == DNS_QR_QUERY &&
					header->OPCode == DNS_OPCODE_QUERY &&
					Portal_DNS_IsSingleQuery(header)) {
					ESPAPP_DEBUGV("DNS: %s -> %s\n",
					Portal_DNS_ParseQueryName(header).c_str(),
					WLAN_PORTAL_IP.toString().c_str());
					Portal_DNS_SendReply(header, packet);
				}
			}
		});
	} else {
		ESPAPP_LOG("ERROR: Unable to start DNS server\n");
		panic();
	}

	AppGlobal.portal.apTestTimer = new Ticker();
	AppGlobal.portal.apTestTimer->attach(10, Portal_TimeAPTest);

	AppGlobal.wsSteps = PORTAL_SETUP;
}

void Portal_Stop() {
	if (AppGlobal.webServer) {
		AppGlobal.webServer->end();
		while (!AppGlobal.webServer->hasFinished()) delay(100);

		delete AppGlobal.webServer;
		AppGlobal.webServer = nullptr;
		delete AppGlobal.webAccounts;
		AppGlobal.webAccounts = nullptr;
		delete AppGlobal.webAuthSessions;
		AppGlobal.webAuthSessions = nullptr;
		ESPAPP_LOG("Device portal has stopped.\n");
		AppGlobal.wsSteps = PORTAL_OFF;
	}
}

void Portal_APTest() {
	AppGlobal.portal.performAPTest = false;

	if (!WiFi.enableSTA(true)) {
		ESPAPP_DEBUG("WARNING: Failed to enable WiFi client mode!\n");
		return;
	}
	if (WiFi.getAutoReconnect()) {
		WiFi.setAutoReconnect(false);
	}
	auto hostname = WiFi.hostname();
	if (hostname == AppConfig.Hostname) {
		WiFi.hostname(AppConfig.Hostname.c_str());
	}
	if (WiFi.begin(AppConfig.WLAN_AP_Name.c_str(), AppConfig.WLAN_AP_Pass.c_str()) == WL_CONNECT_FAILED) {
		ESPAPP_DEBUG("WARNING: Failed to initialize WiFi client!\n");
		return;
	}
	ESPAPP_DEBUG("Probing WiFi access point '%s'...\n",
		AppConfig.WLAN_AP_Name.c_str());
	time_t startTS = GetCurrentTS();
	do {
		delay(500);
		station_status_t WifiStatus = wifi_station_get_connect_status();
		switch (WifiStatus) {
			case STATION_WRONG_PASSWORD:
				ESPAPP_DEBUG("WARNING: Wrong credential for WiFi access point '%s'\n",
					AppConfig.WLAN_AP_Name.c_str());
				startTS = 0;
				break;

			case STATION_GOT_IP:
				ESPAPP_LOG("Connection to WiFi access point '%s' is available!\n",
					AppConfig.WLAN_AP_Name.c_str());
				AppGlobal.portal.performAPTest = true;
				startTS = 0;
				break;
		}
	} while (GetCurrentTS() - startTS < AppConfig.Init_Retry_Cycle);

	WiFi.disconnect(false);
	delay(100);
	WiFi.enableSTA(false);
	if (AppGlobal.portal.performAPTest) {
		// Retry init stage now
		delay(100);
		Init_Start();
	} else {
		ESPAPP_DEBUG("Unable to connect to WiFi access point '%s'\n",
			AppConfig.WLAN_AP_Name.c_str());
	}
}

void loop_PORTAL() {
	Portal_WebServer_Operations();
	if (AppGlobal.portal.performAPTest) Portal_APTest();
}

void perform_DevReset() {
	ESPAPP_DEBUG("Formatting file system...\n");
	if (!VFATFS.format()) {
		ESPAPP_DEBUG("ERROR: Failed to format file system!\n");
		panic();
	}
	SwitchState(APP_DEVRESTART);
}

void perform_DevRestart() {
	ESPAPP_DEBUG("Unmounting file system...\n");
	VFATFS.end();
	ESPAPP_LOG("Restarting device...\n");
	ESP.restart();
	// Should not reach!
	panic();
}

void Service_TimePortal() {
	unsigned int PortalIdle = GetCurrentTS() - AppGlobal.wsActivityTS;
	if (PortalIdle >= AppConfig.Portal_Timeout) {
		AppGlobal.service.portalTimer->detach();
		ESPAPP_DEBUG("Portal idle for %s, shutting down...\n",
			ToString(PortalIdle, TimeUnit::SEC, true).c_str());
		AppGlobal.wsSteps = PORTAL_DOWN;
	}
}

void Service_StartPortal(time_t StartTS) {
	if (AppConfig.Portal_Timeout) {
		if (!AppGlobal.service.portalTimer)
			AppGlobal.service.portalTimer = new Ticker();
		AppGlobal.service.portalTimer->attach(10, Service_TimePortal);
	}
	AppGlobal.wsActivityTS = StartTS;
	AppGlobal.wsSteps = PORTAL_SETUP;
}

void Service_APMonitor() {
	if (WiFi.status() == WL_CONNECTED) {
		if (AppGlobal.service.lastAPAvailableTS) {
			AppGlobal.service.lastAPAvailableTS = 0;
			ESPAPP_DEBUG("Re-connected to WiFi access point!\n");
		}
	} else {
		if (!AppGlobal.service.lastAPAvailableTS) {
			AppGlobal.service.lastAPAvailableTS = GetCurrentTS();
			ESPAPP_DEBUG("Disconnected from WiFi access point, "
				"trying to reconnect...\n")
		} else {
			time_t BreakSpan = GetCurrentTS() - AppGlobal.service.lastAPAvailableTS;
			if (BreakSpan && !(BreakSpan % 10)) {
				ESPAPP_DEBUG("Disconnected from WiFi access point for %s\n",
				ToString(BreakSpan, TimeUnit::SEC, true).c_str());
			}
			if (BreakSpan >= AppConfig.Init_Retry_Count * AppConfig.Init_Retry_Cycle) {
				ESPAPP_DEBUG("Unable to reconnect to WiFi access point, "
					"stopping service...\n");
				AppGlobal.service.portalFallback = true;
			}
		}
	}
}

void Service_Start() {
	SwitchState(APP_SERVICE);

	AppGlobal.service.apTestTimer = new Ticker();
	AppGlobal.service.apTestTimer->attach(1, Service_APMonitor);
	if (AppConfig.Portal_Timeout) {
		Service_StartPortal(AppGlobal.StageTS);
	}

	__userapp_startup();
}

void loop_SERVICE() {
	if (AppGlobal.service.portalFallback) {
		WiFi.disconnect(true);
		delay(100);
		Portal_Start();
		return;
	}
	Portal_WebServer_Operations();

	__userapp_loop();
}

void loop() {
	switch (AppGlobal.State) {
		case APP_STARTUP:
			if (!AppConfig.WLAN_AP_Name.empty()) {
				Init_Start();
			} else {
				ESPAPP_LOG("WiFi access point not configured!\n");
				Portal_Start();
			}
			break;

		case APP_INIT:
			loop_INIT();
			break;

		case APP_PORTAL:
			loop_PORTAL();
			break;

		case APP_SERVICE:
			loop_SERVICE();
			break;

		case APP_DEVRESET:
			perform_DevReset();
			break;

		case APP_DEVRESTART:
			perform_DevRestart();
			break;

		default:
			ESPAPP_LOG("ERROR: Unrecognized state (%d)\n", (int)AppGlobal.State);
			panic();
	}
}

// User-App service routines

void WebPortal_Start() {
	if (AppGlobal.wsSteps == PORTAL_OFF) {
		Service_StartPortal(GetCurrentTS());
	}
}

void WebPortal_Stop() {
	if (AppGlobal.wsSteps > PORTAL_DOWN) {
		AppGlobal.wsSteps = PORTAL_DOWN;
	}
}

extern void Device_Restart() {
	SwitchState(APP_DEVRESTART);
}
