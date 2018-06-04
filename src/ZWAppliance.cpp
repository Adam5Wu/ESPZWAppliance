#include <sys/time.h>
#include <limits.h>

#include <FS.h>
#include <sntp.h>

extern "C" {
	void tune_timeshift64 (uint64_t now_us);
	int settimeofday(const struct timeval *, const struct timezone *);
	extern struct rst_info resetInfo;
}

#include <Ticker.h>
#include <ESP8266WiFi.h>
#include <DNSServer.h>

#include <ArduinoJson.h>

#include <LinkedList.h>
#include <Units.h>
#include <Timezone.h>
#include <vfatfs_api.h>
#include <ESPAsyncUDP.h>
#include <ESPEasyAuth.h>
#include <ESPAsyncWebServer.h>
#include <AsyncJsonResponse.h>

#include "AppBaseUtils.hpp"
#include "ZWApplianceRes.hpp"
#include "RTCMemory.hpp"

#define __ESPZWAppliance_Internal__
#include "ESPZWAppliance.h"

#define WLAN_PORTAL_IP      IPAddress(192, 168, 168, 168)
// Enable gateway for OS portal detection
#define WLAN_PORTAL_GATEWAY WLAN_PORTAL_IP
#define WLAN_PORTAL_SUBNET  IPAddress(255, 255, 255, 0)

#ifndef WLAN_PORTAL_GATEWAY
#define WLAN_PORTAL_GATEWAY IPAddress(0, 0, 0, 0)
#endif

#define RTC_SLOTS_RESERVED      8

#define RTC_SLOT_FLAGS          0
#define RTC_FLAG_BOOTFAIL       0x00000001
#define RTC_FLAG_TIMESYNC       0x00000002
#define RTC_FLAG_RESTORED       0x80000000

#define RTC_SLOT_LASTKNOWNTS    1
#define RTC_CLOCKUPDATE         15  // Update RTC last known clock every 15 seconds

#define SDKBUG_LIGHTSLEEP_POLL

typedef enum {
	APP_STARTUP = 0,
	APP_INIT,
	APP_PORTAL,
	APP_SERVICE,
	APP_DEVRESET,
	APP_DEVRESTART,
} AppState;

ESPAPP_DEBUGDO(
static PGM_P StrAppState(AppState state) {
	switch (state) {
		case APP_STARTUP: return PSTR_L("Startup");
		case APP_INIT: return PSTR_L("Init");
		case APP_PORTAL: return PSTR_L("Portal");
		case APP_SERVICE: return PSTR_L("Service");
		case APP_DEVRESET: return PSTR_L("Device-Reset");
		case APP_DEVRESTART: return PSTR_L("Device-Restart");
		default: return PSTR_L("<Unknown>");
	}
}
)

typedef enum {
	INIT_STA_CONNECT = 0,
	INIT_STA_WPS,
	INIT_NTP_SYNC,
	INIT_FAIL
} InitSteps;

typedef enum {
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
} PortalSteps;

static struct {
	time_t StageTS;
	time_t StartTS;
	bool NoService;
	AppState State;

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
			bool authFailure;
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
			bool reload;
		} service;
	};
} AppGlobal;

static uint32_t RTCFlags;
static Ticker RTCClockUpdate;

static TAPList APList(nullptr);
static time_t APScanLast;

static uint8_t APMAC[6];
static uint8_t APCHAN;

static bool APReceivedIP;
static bool APConnected;
static WiFiDisconnectReason APLastDisconnectReason;

static bool APScanInProgress;

typedef enum {
	WPS_IDLE = 0,
	WPS_SUCCESS,
	WPS_PROGRESS,
	WPS_NOTFOUND,
	WPS_TIMEOUT,
	WPS_NOSUPPORT,
	WPS_FAIL
} WPSStatusCode;

static WPSStatusCode WPSStatus;

#ifdef SDKBUG_LIGHTSLEEP_POLL

static Ticker LWIPTimer;
extern "C" {
	void sys_check_timeouts(void);
}

#define LWIP_TIMER_POLL sys_check_timeouts
#ifndef LWIP_TIMER_POLL
static void LWIP_TIMER_POLL(void) {
	ESPAPP_DEBUG("* LWIP poll...\n");
	sys_check_timeouts();
}
#endif

#endif

time_t GetCurrentTS() {
	struct timeval TV;
	gettimeofday(&TV, nullptr);
	return TV.tv_sec;
}

static struct {
	bool Production;
	bool PersistWLAN;
	bool WLAN_WPS;

	WiFiSleepType PowerSaving;
	float WiFi_Power;

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

static String PrintTime(time_t ts) {
	String StrTime('\0', 25);
	TimeChangeRule* TZ;
	time_t LocTime = AppConfig.TZ.toLocal(ts, &TZ);
	ctime_r(&LocTime, StrTime.begin());
	StrTime.end()[-1] = ' ';
	StrTime.concat(TZ->abbrev);
	return std::move(StrTime);
}

static void Portal_Stop();

extern void __userapp_setup();
extern void __userapp_prestart_loop();
extern bool __userapp_startup();
extern void __userapp_loop();
extern void __userapp_teardown();

static time_t FinalizeCurrentState() {
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

			if (!AppGlobal.NoService)
				__userapp_teardown();
			break;

		case APP_DEVRESET:
			// Do Nothing
			break;

		case APP_DEVRESTART:
			// Do Nothing
			break;

		default:
			ESPAPP_LOG("Unrecognised state (%d)\n", (int)AppGlobal.State);
			panic();
	}
	Portal_Stop();
	time_t curTS = GetCurrentTS();
	ESPAPP_DEBUG("End of state [%s] (%s)\n", SFPSTR(StrAppState(AppGlobal.State)),
		ToString(curTS - AppGlobal.StageTS, TimeUnit::SEC, true).c_str());

	time_t StartTS = AppGlobal.StartTS;
	bool NoService = AppGlobal.NoService;
	memset(&AppGlobal, 0, sizeof(AppGlobal));
	AppGlobal.StartTS = StartTS;
	AppGlobal.NoService = NoService;
	return curTS;
}

static void SwitchState(AppState state) {
	AppGlobal.StageTS = FinalizeCurrentState();
	AppGlobal.State = state;
	ESPAPP_DEBUG("Start of state [%s] @%s\n", SFPSTR(StrAppState(AppGlobal.State)),
		PrintTime(AppGlobal.StageTS).c_str());
}

static Dir get_dir(String const &path) {
	auto Ret = mkdirs(VFATFS, path);
	if (!Ret) {
		ESPAPP_LOG("ERROR: Unable to create directory '%s'\n", path.c_str());
		panic();
	}
	return std::move(Ret);
}

static void init_config_defaults() {
	AppConfig.Production = false;
	AppConfig.PersistWLAN = true;
	AppConfig.PowerSaving = CONFIG_DEFAULT_POWER_SAVING;
	AppConfig.WiFi_Power = CONFIG_DEFAULT_WIFI_POWER;
	AppConfig.WLAN_AP_Name.clear();
	AppConfig.WLAN_AP_Pass.clear();
	AppConfig.WLAN_WPS = true;
	AppConfig.Init_Retry_Count = CONFIG_DEFAULT_INIT_RETRY_COUNT;
	AppConfig.Init_Retry_Cycle = CONFIG_DEFAULT_INIT_RETRY_CYCLE;
	AppConfig.Hostname = String(FL(CONFIG_DEFAULT_HOSTNAME_PFX)) +
		String(ESP.getChipId(), 16);
	AppConfig.Portal_Timeout = CONFIG_DEFAULT_PORTAL_TIMEOUT;
	AppConfig.Portal_APTest = CONFIG_DEFAULT_PORTAL_APTEST;
	AppConfig.NTP_Server.clear();
	AppConfig.TZ.setRules(Timezone::UTC, Timezone::UTC);
}

static int8_t load_config_timezone(JsonObject const &obj, TimeChangeRule &r) {
	if (obj.success()) {
		String Name = obj[FPSTR(CONFIGKEY_TZ_Name)] | String(FL("Local")).c_str();
		// The following parameters are mandatory!
		// Use invalid default value to simplify error handling
		uint8_t Week = obj[FPSTR(CONFIGKEY_TZ_Week)] | -1;
		uint8_t DoW = obj[FPSTR(CONFIGKEY_TZ_DayOfWeek)] | -1;
		uint8_t Month = obj[FPSTR(CONFIGKEY_TZ_Month)] | -1;
		uint8_t Hour = obj[FPSTR(CONFIGKEY_TZ_Hour)] | -1;
		int Offset = obj[FPSTR(CONFIGKEY_TZ_Offset)] | INT_MAX;
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

static WiFiSleepType load_config_powersaving(String const &spec, WiFiSleepType defval) {
	if (spec.equalsIgnoreCase(FL(POWER_SAVING_NONE))) {
		return WIFI_NONE_SLEEP ;
	} else if (spec.equalsIgnoreCase(FL(POWER_SAVING_MODEM))) {
		return WIFI_MODEM_SLEEP ;
	} else if (spec.equalsIgnoreCase(FL(POWER_SAVING_LIGHT))) {
		return WIFI_LIGHT_SLEEP ;
	} else {
		if (spec) {
			ESPAPP_LOG("WARNING: Invalid power saving specification, "
				"use default value!\n");
		}
		return defval;
	}
}

static void load_config_json(JsonObject const &obj) {
	AppConfig.Production = obj[FPSTR(CONFIGKEY_Production)] | AppConfig.Production;
	AppConfig.PersistWLAN = obj[FPSTR(CONFIGKEY_PersistWLAN)] | AppConfig.PersistWLAN;

	AppConfig.PowerSaving = load_config_powersaving(obj[FPSTR(CONFIGKEY_PowerSaving)], AppConfig.PowerSaving);
	AppConfig.WiFi_Power = obj[FPSTR(CONFIGKEY_WiFi_Power)] | AppConfig.WiFi_Power;
	if ((AppConfig.WiFi_Power < 0) || (AppConfig.WiFi_Power> WIFI_POWER_MAX)) {
		ESPAPP_LOG("WARNING: Invalid WiFi output power configuration, "
			"use default value!\n");
		AppConfig.WiFi_Power = CONFIG_DEFAULT_WIFI_POWER;
	}

	AppConfig.WLAN_AP_Name = obj[FPSTR(CONFIGKEY_WLAN_AP_Name)] | AppConfig.WLAN_AP_Name.c_str();
	AppConfig.WLAN_AP_Pass = obj[FPSTR(CONFIGKEY_WLAN_AP_Pass)] | AppConfig.WLAN_AP_Pass.c_str();
	AppConfig.WLAN_WPS = obj[FPSTR(CONFIGKEY_WLAN_WPS)] | AppConfig.WLAN_WPS;

	AppConfig.Init_Retry_Count = obj[FPSTR(CONFIGKEY_Init_Retry_Count)] | AppConfig.Init_Retry_Count;
	AppConfig.Init_Retry_Cycle = obj[FPSTR(CONFIGKEY_Init_Retry_Cycle)] | AppConfig.Init_Retry_Cycle;

	AppConfig.Hostname = obj[FPSTR(CONFIGKEY_Hostname)] | AppConfig.Hostname.c_str();
	AppConfig.Portal_Timeout = obj[FPSTR(CONFIGKEY_Portal_Timeout)] | AppConfig.Portal_Timeout;
	AppConfig.Portal_APTest = obj[FPSTR(CONFIGKEY_Portal_APTest)] | AppConfig.Portal_APTest;

	AppConfig.NTP_Server = obj[FPSTR(CONFIGKEY_NTP_Server)] | AppConfig.NTP_Server.c_str();
	do {
		// Load timezone configurations
		TimeChangeRule TZR, TZD;
		if (load_config_timezone(obj[FPSTR(CONFIGKEY_TimeZone_Regular)].as<JsonObject&>(), TZR) != 0) {
			ESPAPP_LOG("WARNING: Failed to parse regular timezone configuration, "
				"use default timezone!\n");
			break;
		}
		if (load_config_timezone(obj[FPSTR(CONFIGKEY_TimeZone_Daylight)].as<JsonObject&>(), TZD) != 0) {
			if (!obj.containsKey(FPSTR(CONFIGKEY_TimeZone_Daylight))) {
				ESPAPP_LOG("WARNING: Failed to parse daylight-saving timezone configuration, "
					"use regular timezone!\n");
			}
			TZD = TZR;
		}
		AppConfig.TZ.setRules(TZD, TZR);
	} while (false);
}

static JsonManagerResults load_config(String const &filename,
	std::function<void(JsonObject const &obj)> const &load_cb) {
	auto ConfigDir = get_dir(FL(CONFIG_DIR));
	return JsonManager(ConfigDir, filename, true,
		[&](JsonObject &obj, BoundedDynamicJsonBuffer &parsebuf) {
			load_cb(obj);
			return false;
		}, [&](File &file) {
			return file.truncate(0);
		});
}

static JsonManagerResults update_config(String const &filename,
	JsonObjectCallback const &update_cb) {
	auto ConfigDir = get_dir(FL(CONFIG_DIR));
	return JsonManager(ConfigDir, filename, true, update_cb, [&](File &file) {
		return file.truncate(0);
	});
}

static void InitBootTime(RTCMemory &RTCMem) {
	time_t baseTime;
	if (RTCFlags & RTC_FLAG_TIMESYNC) {
		if (RTCMem.Read(RTC_SLOT_LASTKNOWNTS, (uint32_t*)&baseTime, 1)) {
			// Restore SNTP clock, not exactly accurate, but good enough
			struct timeval tv = { baseTime, 0 };
			settimeofday(&tv, nullptr);
			return;
		}
		ESPAPP_DEBUG("WARNING: Error loading last known time from RTC\n");
		RTCFlags &= ~RTC_FLAG_TIMESYNC;
	}
	struct tm BootTM;
	BootTM.tm_year = 2018 - 1900;
	BootTM.tm_mon = 0;
	BootTM.tm_mday = 1;
	BootTM.tm_hour = 0;
	BootTM.tm_min = 0;
	BootTM.tm_sec = 0;
	baseTime= mktime(&BootTM);
	tune_timeshift64(baseTime * 1000000ull);
}

static PGM_P resetReasonToStr(uint32_t reason) {
	switch (reason) {
		case 0: return PSTR_L("Power on");
		case 1: return PSTR_L("Hard WDT");
		case 2: return PSTR_L("Fatal Exception");
		case 3: return PSTR_L("Soft WDT");
		case 4: return PSTR_L("Soft reset");
		case 5: return PSTR_L("Deep sleep");
		case 6: return PSTR_L("Hard reset");
		default: return PSTR_L("Unknown");
	}
}

static void WiFiEvent_Connected(const WiFiEventStationModeConnected& evt);
static void WiFiEvent_ReceivedIP(const WiFiEventStationModeGotIP& evt);
static void WiFiEvent_Disconnected(const WiFiEventStationModeDisconnected& evt);

WiFiEventHandler onConnected, onReceivedIP, onDisconnected;

void setup() {
	Serial.begin(115200);
	delay(100);
	Serial.println();
	Serial.println(FL("ESP8266 ZWAppliance Core v" ZWAPP_VERSION));

	// Initialize/check RTC memory
	RTCMemory &RTCMem = RTCMemory::Manager();
	if (!RTCMem.Read(RTC_SLOT_FLAGS, &RTCFlags, 1)) {
		ESPAPP_LOG("WARNING: Failed to load appliance flags from RTC\n");
		RTCFlags = 0;
	}

	ESPAPP_DEBUGV("Marking appliance flags in RTC...\n");
	uint32_t BootRTCFlags = RTCFlags | RTC_FLAG_RESTORED | RTC_FLAG_BOOTFAIL;
	if (!RTCMem.Write(RTC_SLOT_FLAGS, &BootRTCFlags, 1)) {
		ESPAPP_DEBUG("WARNING: Failed to update appliance flags to RTC\n");
	}

	// Set a reasonable start time
	InitBootTime(RTCMem);

	// Initialize global states
	memset(&AppGlobal, 0, sizeof(AppGlobal));
	//AppGlobal.State = APP_STARTUP;
	AppGlobal.StartTS = GetCurrentTS();
	AppGlobal.StageTS = AppGlobal.StartTS;
	//AppGlobal.wsSteps = PORTAL_OFF;

	APScanLast = 0;
	APScanInProgress = false;

	WPSStatus = WPS_IDLE;
	APReceivedIP = false;
	APConnected = false;
	APLastDisconnectReason = WIFI_DISCONNECT_REASON_UNSPECIFIED;

	// Base on boot reason, decide whether we should bypass service
	ESPAPP_DEBUG("Boot reason: %s\n",
		SFPSTR(resetReasonToStr(resetInfo.reason)));

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
	init_config_defaults();
	if (load_config(APPLIANCE_CONFIG_FILE, load_config_json) >= JSONMAN_ERR) {
		ESPAPP_LOG("ERROR: Error loading appliance configuration file\n");
		panic();
	}

	ESPAPP_DEBUG("Initializing WiFi...\n");
	if (!AppConfig.PersistWLAN) {
		if (WiFi.getMode() != WIFI_OFF) {
			if (!WiFi.mode(WIFI_OFF)) {
				ESPAPP_LOG("WARNING: Failed to disable WiFi!\n");
			}
		}
		// Order is important
		WiFi.persistent(false);
	}
	if (!WiFi.getAutoConnect()) {
		WiFi.setAutoConnect(true);
	}
	if (!WiFi.getAutoReconnect()) {
		WiFi.setAutoReconnect(true);
	}

	if (WiFi.getSleepMode() != AppConfig.PowerSaving) {
		if (!WiFi.setSleepMode(AppConfig.PowerSaving)) {
			ESPAPP_LOG("WARNING: Failed to configure energy saving!\n");
		}
	}
#ifdef SDKBUG_LIGHTSLEEP_POLL
	if (WiFi.getSleepMode() == WIFI_LIGHT_SLEEP) {
		ESPAPP_DEBUG("Enabling supplemental LWIP timer...\n");
		LWIPTimer.attach(1, LWIP_TIMER_POLL);
	}
#endif
	if (WiFi.getPhyMode() != WIFI_PHY_MODE_11N) {
		if (!WiFi.setPhyMode(WIFI_PHY_MODE_11N)) {
			ESPAPP_LOG("WARNING: Failed to configure WiFi in 802.11n mode!\n");
		}
	}
	WiFi.setOutputPower(AppConfig.WiFi_Power);

	onConnected = WiFi.onStationModeConnected(WiFiEvent_Connected);
	onReceivedIP = WiFi.onStationModeGotIP(WiFiEvent_ReceivedIP);
	onDisconnected = WiFi.onStationModeDisconnected(WiFiEvent_Disconnected);

	if (!WiFi.mode(WIFI_STA)) {
		ESPAPP_LOG("ERROR: Failed to enable WiFi client mode!\n");
		panic();
	}
	auto hostname = WiFi.hostname();
	if (hostname != AppConfig.Hostname) {
		WiFi.hostname(AppConfig.Hostname.c_str());
	}

	if ((resetInfo.reason == 1) ||	// Hard WDT
		(resetInfo.reason == 2) ||	// Fatal Exception
		(resetInfo.reason == 3)) {	// Soft WDT
		do {
			if (AppConfig.Production) {
				if (!(RTCFlags & RTC_FLAG_BOOTFAIL)) {
					ESPAPP_LOG("WARNING: Detected non-trivial service failure, resuming...\n");
					break;
				}
				ESPAPP_LOG("WARNING: Detected trivial service failure, bypassing...\n");
			} else {
				ESPAPP_DEBUG("WARNING: Service failed in previous boot, bypassing...\n");
			}
			AppGlobal.NoService = true;
		} while(false);
	}

	if (!AppGlobal.NoService)
		__userapp_setup();
}

static void WiFiEvent_Connected(const WiFiEventStationModeConnected& evt) {
	ESPAPP_DEBUGVV("- WiFi connected!\n");
	APLastDisconnectReason = WIFI_DISCONNECT_REASON_UNSPECIFIED;
	APConnected = APReceivedIP;
	memcpy(APMAC, evt.bssid, 6);
	APCHAN = evt.channel;
}

static void WiFiEvent_ReceivedIP(const WiFiEventStationModeGotIP& evt) {
	ESPAPP_DEBUGVV("- WiFi obtained IP!\n");
	APConnected = APReceivedIP = true;
}

static void WiFiEvent_Disconnected(const WiFiEventStationModeDisconnected& evt) {
	ESPAPP_DEBUGVV("WiFi disconnection (reason %d)\n", evt.reason);
	APConnected = false;

	if ((AppGlobal.State == APP_INIT) && (AppGlobal.init.steps == INIT_STA_CONNECT)) {
		switch (evt.reason) {
			case WIFI_DISCONNECT_REASON_AUTH_EXPIRE:
			case WIFI_DISCONNECT_REASON_AUTH_FAIL:
				// Need two repeated code to confirm
				if (APLastDisconnectReason != evt.reason) break;
				AppGlobal.init.authFailure = true;
		}
		APLastDisconnectReason = evt.reason;
	}
}

static void Init_Start() {
	SwitchState(APP_INIT);
	if (AppConfig.PersistWLAN) {
		WiFi.persistent(true);
	}
	//AppGlobal.init.steps = INIT_STA_CONNECT;
	AppGlobal.init.cycleTS = AppGlobal.init.lastKnownTS = GetCurrentTS();
	//AppGlobal.init.authFailure = false;

	auto Status = WiFi.begin(AppConfig.WLAN_AP_Name.c_str(), AppConfig.WLAN_AP_Pass.c_str(),
		0, nullptr, false);
	if (Status != WL_CONNECTED) {
		ESPAPP_LOG("Connecting to WiFi access point '%s'...\n",
			AppConfig.WLAN_AP_Name.c_str());
		if (!AppConfig.PersistWLAN) WiFi.reconnect();
		delay(100);
	}
}

static void Portal_Start();
static void Service_Start();

static void Init_NTP_Sync() {
	configTime(0, 0, AppConfig.NTP_Server.c_str());
	delay(100);
}

static void Init_NTP() {
	AppGlobal.init.steps = INIT_NTP_SYNC;
	if (AppConfig.NTP_Server) {
		ESPAPP_DEBUG("Synchronizing with NTP server '%s'...\n",
			AppConfig.NTP_Server.c_str());
		Init_NTP_Sync();
	}
}

static void Init_TimeoutTrigger() {
	ESPAPP_DEBUGV("WiFi network initialization did not complete in time\n");
	AppGlobal.init.steps = INIT_FAIL;
}

static void updateRTCClock() {
	time_t curTS = GetCurrentTS();
	RTCMemory &RTCMem = RTCMemory::Manager();
	if (RTCMem.Write(RTC_SLOT_LASTKNOWNTS, (uint32_t*)&curTS, 1)) {
		if (!(RTCFlags & RTC_FLAG_TIMESYNC)) {
			RTCFlags |= RTC_FLAG_TIMESYNC;
			if (!RTCMem.Write(RTC_SLOT_FLAGS, &RTCFlags, 1)) {
				ESPAPP_DEBUG("WARNING: Failed to update appliance flags to RTC\n");
			}
		}
	} else {
		ESPAPP_DEBUG("WARNING: Failed to update last known time to RTC\n");
	}
}

static void WPS_Finished(wps_cb_status status) {
	if(!wifi_wps_disable()) {
		ESPAPP_DEBUG("WARNING: Error leaving WPS mode!\n");
	}
	switch (status) {
		case WPS_CB_ST_SUCCESS:
			ESPAPP_DEBUG("WPS successful!\n");
			WPSStatus = WPS_SUCCESS;
			wifi_station_connect();
		break;
		case WPS_CB_ST_TIMEOUT:
			ESPAPP_DEBUG("WPS timed out!\n");
			WPSStatus = WPS_TIMEOUT;
		break;
		case WPS_CB_ST_WEP:
			ESPAPP_DEBUG("WPS required WEP support!\n");
			WPSStatus = WPS_NOSUPPORT;
		break;
		case WPS_CB_ST_UNK:
			ESPAPP_DEBUG("WPS unable to locate access point!\n");
			WPSStatus = WPS_NOTFOUND;
		break;
		case WPS_CB_ST_FAILED:
			ESPAPP_DEBUG("WPS negotiation failed!\n");
			WPSStatus = WPS_FAIL;
			break;
		default:
			ESPAPP_DEBUG("WPS unrecognised status (%d)\n", status);
			WPSStatus = WPS_FAIL;
	}
}

static bool WPS_Start() {
	if (WPSStatus == WPS_PROGRESS) {
		ESPAPP_LOG("WARNING: WPS already in progress!\n");
		return false;
	}
	WPSStatus = WPS_IDLE;
	if(!wifi_wps_enable(WPS_TYPE_PBC)) {
		ESPAPP_DEBUG("Unable to enter WPS mode!\n");
		return false;
	}
	if(!wifi_set_wps_cb((wps_st_cb_t)&WPS_Finished)) {
		ESPAPP_DEBUG("Unable to configure WPS callback!\n");
        return false;
    }
	if(!wifi_wps_start()) {
		ESPAPP_DEBUG("Unable to start WPS procedure!\n");
        return false;
    }
	WPSStatus = WPS_PROGRESS;
	return true;
}

static void loop_INIT() {
	switch (AppGlobal.init.steps) {
		case INIT_STA_CONNECT: {
			if (APConnected) {
				ESPAPP_DEBUG("Connected to WiFi access point '%s'\n",
				AppConfig.WLAN_AP_Name.c_str());
				ESPAPP_LOG("IP address: %s\n", WiFi.localIP().toString().c_str());
				ESPAPP_DEBUG("Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
				ESPAPP_DEBUG("Name Server: %s\n", WiFi.dnsIP().toString().c_str());
				Init_NTP();
			} else {
				if (AppGlobal.init.authFailure) {
					if (!AppConfig.WLAN_WPS) {
						ESPAPP_DEBUG("WARNING: Wrong credential for WiFi access point '%s'\n",
							AppConfig.WLAN_AP_Name.c_str());
						AppGlobal.init.steps = INIT_FAIL;
					} else {
						if (WPS_Start()) {
							ESPAPP_LOG("Starting WPS with WiFi access point '%s'...\n",
								AppConfig.WLAN_AP_Name.c_str());
							AppGlobal.init.steps = INIT_STA_WPS;
						} else {
							ESPAPP_DEBUG("Failed to start WPS with WiFi access point '%s'\n",
								AppConfig.WLAN_AP_Name.c_str());
							AppGlobal.init.steps = INIT_FAIL;
						}
					}
				} else {
					AppGlobal.init.lastKnownTS = GetCurrentTS();
					time_t ConnectSpan = AppGlobal.init.lastKnownTS - AppGlobal.init.cycleTS;
					if (ConnectSpan < AppConfig.Init_Retry_Cycle) {
						ESPAPP_DEBUGVVDO({
							time_t cycleSpan = AppConfig.Init_Retry_Cycle - ConnectSpan;
							if (cycleSpan != AppGlobal.init.cycleSpan) {
								AppGlobal.init.cycleSpan = cycleSpan;
								ESPAPP_LOG("Waiting for WiFi connection... (%s to next retry)\n",
									ToString(cycleSpan, TimeUnit::SEC, true).c_str());
							}
						});
						delay(100);
						break;
					}
					if (++AppGlobal.init.cycleCount < AppConfig.Init_Retry_Count) {
						ESPAPP_DEBUGV("WiFi access point connect timeout, retrying... (%d retries left)\n",
							AppConfig.Init_Retry_Count - AppGlobal.init.cycleCount);
						AppGlobal.init.cycleTS = AppGlobal.init.lastKnownTS;
						WiFi.reconnect();
						delay(100);
					} else Init_TimeoutTrigger();
				}
			}
		} break;

		case INIT_STA_WPS: {
			switch (WPSStatus) {
				// WPS in progress
				case WPS_PROGRESS: delay(100); break;
				// WPS succeeded
				case WPS_SUCCESS: {
					struct station_config staConfig;
					if (wifi_station_get_config(&staConfig)) {
						AppConfig.WLAN_AP_Pass = (char *)staConfig.password;
						ESPAPP_DEBUGVV("WPS obtained WiFi access point password '%s'\n",
							AppConfig.WLAN_AP_Pass.c_str());
						auto JMRet = update_config(APPLIANCE_CONFIG_FILE,
							[&](JsonObject & obj, BoundedDynamicJsonBuffer & buf) {
								obj[FL("WLAN_AP_Pass")] = AppConfig.WLAN_AP_Pass;
								return true;
							});
						if (JMRet != JSONMAN_OK_UPDATED) {
							ESPAPP_DEBUG("WARNING: Unable to update WLAN password - "
								"unexpected json config manager result (%d)\n", JMRet);
						}
					} else {
						ESPAPP_DEBUG("WARNING: Unable to obtain WiFi access point password\n");
						ESPAPP_DEBUG("WARNING: Another round of WPS may be necessary after reset\n");
					}
					// Connection has already started, wait for complete...
					APLastDisconnectReason = WIFI_DISCONNECT_REASON_UNSPECIFIED;
					AppGlobal.init.authFailure = false;
					AppGlobal.init.cycleTS = GetCurrentTS();
					AppGlobal.init.steps = INIT_STA_CONNECT;
				} break;
				// WPS unsuccessful
				default: {
					AppGlobal.init.steps = INIT_FAIL;
				}
			}
		} break;

		case INIT_NTP_SYNC: {
			if (AppConfig.NTP_Server) {
				if (!(RTCFlags & RTC_FLAG_TIMESYNC)) {
					time_t curTS = sntp_get_current_timestamp();
					if (curTS < AppGlobal.init.lastKnownTS) {
						AppGlobal.init.lastKnownTS = GetCurrentTS();
						time_t ConnectSpan = AppGlobal.init.lastKnownTS - AppGlobal.init.cycleTS;
						if (ConnectSpan < AppConfig.Init_Retry_Cycle) {
							ESPAPP_DEBUGVVDO({
								time_t cycleSpan = AppConfig.Init_Retry_Cycle - ConnectSpan;
								if (cycleSpan != AppGlobal.init.cycleSpan) {
									AppGlobal.init.cycleSpan = cycleSpan;
									ESPAPP_LOG("Waiting for NTP synchronization... "
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
							AppGlobal.init.cycleTS = AppGlobal.init.lastKnownTS;
							Init_NTP_Sync();
						} else Init_TimeoutTrigger();
						break;
					}
					ESPAPP_LOG("NTP time synchronized @%s\n", PrintTime(curTS).c_str());
					// Update RTC with syncrhonized time
					RTCMemory &RTCMem = RTCMemory::Manager();
					if (RTCMem.Write(RTC_SLOT_LASTKNOWNTS, (uint32_t*)&curTS, 1)) {
						RTCFlags |= RTC_FLAG_TIMESYNC;
						if (!RTCMem.Write(RTC_SLOT_FLAGS, &RTCFlags, 1)) {
							ESPAPP_DEBUG("WARNING: Failed to update appliance flags to RTC\n");
						}
					} else {
						ESPAPP_DEBUG("WARNING: Failed to update last known time to RTC\n");
					}
					// Update the stage timestamp
					time_t StageTime = AppGlobal.init.lastKnownTS - AppGlobal.StageTS;
					AppGlobal.StageTS = curTS - StageTime;
					// Update the boot timestamp
					time_t UpTime = AppGlobal.init.lastKnownTS - AppGlobal.StartTS;
					ESPAPP_DEBUG("Uptime: %s\n", ToString(UpTime, TimeUnit::SEC, true).c_str());
					AppGlobal.StartTS = curTS - UpTime;
				} else {
					// We have a pretty good time reference, no need to wait
					time_t curTS = AppGlobal.init.lastKnownTS;
					// Let SNTP continue to work in the background
					ESPAPP_LOG("NTP background synchronizing @%s\n", PrintTime(curTS).c_str());
					time_t UpTime = curTS - AppGlobal.StartTS;
					ESPAPP_DEBUG("Uptime: %s\n", ToString(UpTime, TimeUnit::SEC, true).c_str());
				}
				// Schedule automatic RTC updates
				if (!RTCClockUpdate.active()) {
					RTCClockUpdate.attach(RTC_CLOCKUPDATE, updateRTCClock);
				}
			} else {
				ESPAPP_DEBUG("NTP server not configured, synchronization skipped\n");
			}
			Service_Start();
		} break;

		case INIT_FAIL: {
			ESPAPP_LOG("WiFi network initialization failed!\n");
			if (AppConfig.PersistWLAN) {
				WiFi.persistent(false);
			}
			// Turn off STA
			WiFi.disconnect(true);
			delay(100);
			Portal_Start();
		} break;

		default: {
			ESPAPP_LOG("ERROR: Unrecognised init step (%d)\n",
				(int)AppGlobal.init.steps);
			panic();
		}
	}
}

#include "API.Config.hpp"
#include "API.APScan.hpp"
#include "API.OTA.hpp"

static bool Portal_StartAPScan() {
	APScanInProgress = true;
	if (AppGlobal.State == APP_PORTAL) {
		return !AppGlobal.portal.performAPTest;
	}
	return true;
}

static void Portal_StopAPScan() {
	APScanInProgress = false;
}

void Portal_WebServer_RespondBuiltInData(AsyncWebRequest &request,
	PGM_P data, String const &filename, int code = 200) {
	ESPAPP_DEBUG("* Serving built-in data for '%s'...\n", request.url().c_str());
	request.send_P(code, data, AsyncFileResponse::contentTypeByName(filename));
}

void Portal_WebServer_RespondFileOrBuiltIn(AsyncWebRequest &request,
	String const &filename, PGM_P defdata, int code = 200) {
	auto PortalDir = get_dir(FL(PORTAL_DIR));
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

struct StaticResDefaults {
	PGM_P Path;
	PGM_P Content;
};
static LinkedList<StaticResDefaults> *PortalStaticResMap = nullptr;

static void Portal_WebServer_Operations() {
	switch (AppGlobal.wsSteps) {
		case PORTAL_OFF:
		case PORTAL_UP: {
			// Do Nothing
		} break;

		case PORTAL_DOWN: {
			Portal_Stop();
		} break;

		case PORTAL_SETUP: {
			ESPAPP_DEBUG("Bringing up portal service...\n");
			AppGlobal.webServer = new AsyncWebServer(80);
			AppGlobal.webServer->configRealm(AppConfig.Hostname);
			AppGlobal.wsSteps = PORTAL_ACCOUNT;
		} break;

		case PORTAL_ACCOUNT: {
			auto ConfigDir = get_dir(FL(CONFIG_DIR));
			ESPAPP_DEBUG("Loading portal accounts...\n");

			AppGlobal.webAccounts = new HTTPDigestAccountAuthority(AppConfig.Hostname);
			File AccountData = ConfigDir.openFile(FL(PORTAL_ACCOUNTS_FILE), "r");
			if (AccountData) {
				int AccountCnt = AppGlobal.webAccounts->loadAccounts(AccountData);
				ESPAPP_DEBUG("* Loaded %d accounts.\n", AccountCnt);
				if (AppGlobal.webAccounts->getIdentity(FL(PORTAL_ADMIN_USER)) == IdentityProvider::UNKNOWN) {
					ESPAPP_DEBUG("WARNING: default administrator account '%s' not found, "
						"re-initializing...\n",
						SFPSTR(FL(PORTAL_ADMIN_USER)));
					AppGlobal.webAccounts->addAccount(FL(PORTAL_ADMIN_USER), FL(PORTAL_ADMIN_USER));
				}
			} else {
				ESPAPP_DEBUG("WARNING: accounts file '%s' not found, using built-in accounts...\n",
					SFPSTR(FL(PORTAL_ACCOUNTS_FILE)));
				AppGlobal.webAccounts->addAccount(FL(PORTAL_ADMIN_USER), FL(PORTAL_ADMIN_USER));
			}
			AppGlobal.webAuthSessions = new SessionAuthority(AppGlobal.webAccounts,
				AppGlobal.webAccounts);
			AppGlobal.wsSteps = PORTAL_ACCESSCTRL;
		} break;

		case PORTAL_ACCESSCTRL: {
			auto ConfigDir = get_dir(FL(CONFIG_DIR));
			ESPAPP_DEBUG("Loading access control...\n");
			{
				File ACLData = Portal_WebServer_CheckRestoreRes(ConfigDir,
					FL(PORTAL_ACCESS_FILE), PORTAL_DEFAULT_ACL);
				if (ACLData) {
					AppGlobal.webServer->configAuthority(*AppGlobal.webAuthSessions, ACLData);
				} else {
					StreamString ACLData(FPSTR(PORTAL_DEFAULT_ACL));
					AppGlobal.webServer->configAuthority(*AppGlobal.webAuthSessions, ACLData);
				}
			}

			AuthSession fakeAnonyAuth(IdentityProvider::ANONYMOUS, nullptr);
			if (AppGlobal.webServer->_checkACL(HTTP_GET, FL(PORTAL_ROOT), &fakeAnonyAuth) != ACL_ALLOWED) {
				ESPAPP_DEBUG("WARNING: correcting ACL to allow '%s' access to portal root...\n",
					SFPSTR(FL(ANONYMOUS_ID)));
				AppGlobal.webServer->_prependACL(FL(PORTAL_ROOT), HTTP_BASIC_READ,
					{nullptr, {&IdentityProvider::ANONYMOUS}});
			}
			{
				auto &AdminIdent = AppGlobal.webAccounts->getIdentity(FL(PORTAL_ADMIN_USER));
				AuthSession fakeAdminAuth(AdminIdent, nullptr);
				if (AppGlobal.webServer->_checkACL(HTTP_PUT, FL(PORTAL_FSDAV_ROOT), &fakeAdminAuth) != ACL_ALLOWED ||
					AppGlobal.webServer->_checkACL(HTTP_PUT, FL(PORTAL_FSDAV_ROOT), &fakeAnonyAuth) == ACL_ALLOWED) {
					ESPAPP_DEBUG("WARNING: correcting ACL to allow '%s' exclusive access to '%s'...\n",
						SFPSTR(FL(PORTAL_ADMIN_USER)), SFPSTR(FL(PORTAL_FSDAV_ROOT)));
					AppGlobal.webServer->_prependACL(FL(PORTAL_FSDAV_ROOT), HTTP_ANY,
						{nullptr, {&AdminIdent}});
				}
				if (AppGlobal.webServer->_checkACL(HTTP_GET, FL(PORTAL_API_HWCTL), &fakeAdminAuth) != ACL_ALLOWED ||
					AppGlobal.webServer->_checkACL(HTTP_GET, FL(PORTAL_API_HWCTL), &fakeAnonyAuth) == ACL_ALLOWED) {
					ESPAPP_DEBUG("WARNING: correcting ACL to allow '%s' exclusive access to '%s'...\n",
						SFPSTR(FL(PORTAL_ADMIN_USER)), SFPSTR(FL(PORTAL_API_HWCTL)));
					AppGlobal.webServer->_prependACL(FL(PORTAL_API_HWCTL), HTTP_BASIC_READ,
						{nullptr, {&AdminIdent}});
				}
			}
			AppGlobal.wsSteps = PORTAL_FILES;
		} break;

		case PORTAL_FILES: {
			auto PortalDir = get_dir(FL(PORTAL_DIR));

			PortalStaticResMap = new LinkedList<StaticResDefaults>(nullptr);
			PortalStaticResMap->append({PSTR_L(PORTAL_ROOT PORTAL_PAGE_OTA), PORTAL_RESDATA_OTA_HTML});
			PortalStaticResMap->append({PSTR_L(PORTAL_ROOT PORTAL_RES_MD5JS), PORTAL_RESDATA_MD5_JS});
			PortalStaticResMap->append({PSTR_L(PORTAL_ROOT PORTAL_RES_OTACOREJS), PORTAL_RESDATA_OTACORE_JS});

			PortalStaticResMap->append({PSTR_L(PORTAL_ROOT PORTAL_PAGE_SYSCONFIG), PORTAL_RESDATA_SYSCONFIG_HTML});
			PortalStaticResMap->append({PSTR_L(PORTAL_ROOT PORTAL_RES_JQUERYJS), PORTAL_RESDATA_JQUERY_JS});
			PortalStaticResMap->append({PSTR_L(PORTAL_ROOT PORTAL_RES_APSCANCOREJS), PORTAL_RESDATA_APSCANCORE_JS});

			AppGlobal.wsSteps = PORTAL_START;
		} break;

		case PORTAL_START: {
			bool isCaptive = AppGlobal.State == APP_PORTAL;

			if (isCaptive) {
				// Redirect all request to this host (and hence being captive)
				auto pHandler = new AsyncHostRedirWebHandler(AppConfig.Hostname, HTTP_ANY);
				pHandler->altPath = PORTAL_ROOT;
				pHandler->psvPaths.append(FL(PORTAL_WPAD_FILE));
				AppGlobal.webServer->addHandler(pHandler).addFilter(
					[&](AsyncWebRequest const &request) {
						// Record last activity time stamp
						AppGlobal.wsActivityTS = GetCurrentTS();
						return true;
					});
			} else {
				AppGlobal.webServer->addHandler(new AsyncPassthroughWebHandler()).addFilter(
					[&](AsyncWebRequest const &request) {
						// Record last activity time stamp
						AppGlobal.wsActivityTS = GetCurrentTS();
						return false;
					});
			}

			{
				if (isCaptive) {
					auto &Handler = AppGlobal.webServer->on(FL(PORTAL_WPAD_FILE"$"),
						[](AsyncWebRequest &request) {
							request.send(404);
						});
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_HWCTL_DEVRESET"$"),
					[](AsyncWebRequest &request) {
						Portal_WebServer_RespondFileOrBuiltIn(request,
							FL(PORTAL_PAGE_DEVRESET), PORTAL_RESDATA_DEVRESET_HTML);
						AppGlobal.wsSteps = PORTAL_DEVRESET;
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_HWCTL_DEVRESTART"$"),
					[](AsyncWebRequest &request) {
						Portal_WebServer_RespondFileOrBuiltIn(request,
							FL(PORTAL_PAGE_DEVRESTART), PORTAL_RESDATA_DEVRESTART_HTML);
						AppGlobal.wsSteps = PORTAL_DEVRESTART;
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_HWMON_HEAP"$"),
					[](AsyncWebRequest &request) {
						size_t FreeHeap = ESP.getFreeHeap();
						request.send(200, String(FreeHeap), FL("text/plain"));
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_HWMON_UPTIME"$"),
					[](AsyncWebRequest &request) {
						time_t UpTime = GetCurrentTS() - AppGlobal.StartTS;
						request.send(200, String(UpTime), FL("text/plain"));
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_HWMON),
					[](AsyncWebRequest &request) {
						size_t FreeHeap = ESP.getFreeHeap();
						time_t UpTime = GetCurrentTS() - AppGlobal.StartTS;
						AsyncJsonResponse * response =
							AsyncJsonResponse::CreateNewObjectResponse();
						response->root[FL("heap")] = FreeHeap;
						response->root[FL("uptime")] = UpTime;
						request.send(response);
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_VERSION_ZWAPP"$"),
				[](AsyncWebRequest &request) {
					request.send_P(200, PSTR_L(ZWAPP_VERSION), FL("text/plain"));
				});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_STATE_CLOCK"$"),
					[](AsyncWebRequest &request) {
						time_t utc_clock = sntp_get_current_timestamp();
						TimeChangeRule* TZ;
						time_t clock = AppConfig.TZ.toLocal(utc_clock, &TZ);
						struct tm tm_out;
						localtime_r(&clock, &tm_out);
						AsyncJsonResponse * response =
							AsyncJsonResponse::CreateNewObjectResponse();
						response->root[FL("u")] = utc_clock;
						response->root[FL("Y")] = tm_out.tm_year+1900;
						response->root[FL("m")] = tm_out.tm_mon+1;
						response->root[FL("d")] = tm_out.tm_mday;
						response->root[FL("w")] = tm_out.tm_wday;
						response->root[FL("H")] = tm_out.tm_hour;
						response->root[FL("M")] = tm_out.tm_min;
						response->root[FL("s")] = tm_out.tm_sec;
						response->root[FL("Z")] = TZ->abbrev;
						response->root[FL("z")] = TZ->offset;
						request.send(response);
					});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_STATE_WLAN"$"),
				[](AsyncWebRequest &request) {
					AsyncJsonResponse * response =
						AsyncJsonResponse::CreateNewObjectResponse();
					JsonObject &Root = response->root.as<JsonObject&>();
					auto WiFiMode = WiFi.getMode();
					if ((WiFiMode == WIFI_AP) || (WiFiMode == WIFI_AP_STA)) {
						JsonObject &APInfo = Root.createNestedObject(FL("AP"));
						{
							// Collect AP info
							struct softap_config apConfig;
							if (wifi_softap_get_config(&apConfig)) {
								if (apConfig.ssid_len) {
									String apSSID((char *)apConfig.ssid, apConfig.ssid_len);
									APInfo[FL("SSID")] = apSSID;
								} else APInfo[FL("SSID")] = (char *)apConfig.ssid;
								APInfo[FL("Channel")] = apConfig.channel;
								APInfo[FL("Auth")] = PrintAuth(apConfig.authmode);
								if (apConfig.authmode != AUTH_OPEN) {
									APInfo[FL("Pass")] = (char *)apConfig.password;
								}
							} else {
								ESPAPP_DEBUG("WARNING: failed to retrieve WiFi base station info.\n");
							}
						}
						{
							// Collect connected clients
							JsonArray &Clients = APInfo.createNestedArray(FL("Stations"));
							struct station_info *staInfo = wifi_softap_get_station_info();
							while (staInfo) {
								JsonArray &Station = Clients.createNestedArray();
								Station.add(PrintMAC(staInfo->bssid));
								Station.add(PrintIP(staInfo->ip.addr));
								staInfo	= STAILQ_NEXT(staInfo, next);
							}
							wifi_softap_free_station_info();
						}
					}
					if ((WiFiMode == WIFI_STA) || (WiFiMode == WIFI_AP_STA)) {
						JsonObject &STAInfo = Root.createNestedObject(FL("STA"));
						{
							struct station_config staConfig;
							if (wifi_station_get_config(&staConfig)) {
								STAInfo[FL("APName")] = (char *)staConfig.ssid;
								STAInfo[FL("APPass")] = (char *)staConfig.password;
								STAInfo[FL("Connected")] = APConnected;
								if (APConnected) {
									STAInfo[FL("APChannel")] = APCHAN;
									STAInfo[FL("APMAC")] = PrintMAC(APMAC);
								} else {
									if (staConfig.bssid_set) {
										STAInfo[FL("APMAC")] = PrintMAC(staConfig.bssid);
									}
								}
							} else {
								ESPAPP_DEBUG("WARNING: failed to retrieve WiFi client info.\n");
							}
						}
					}
					request.send(response);
				});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->on(FL(PORTAL_API_STATE_CONFIG_ZWAPP"$"),
				[](AsyncWebRequest &request) {
					AsyncJsonResponse * response =
						AsyncJsonResponse::CreateNewObjectResponse();
					JsonObject &Root = response->root.as<JsonObject&>();

					Root[FPSTR(CONFIGKEY_Production)] = AppConfig.Production;
					Root[FPSTR(CONFIGKEY_PersistWLAN)] = AppConfig.PersistWLAN;
					switch(AppConfig.PowerSaving) {
						case WIFI_NONE_SLEEP:
							Root[FPSTR(CONFIGKEY_PowerSaving)] = FL(POWER_SAVING_NONE);
							break;
						case WIFI_MODEM_SLEEP:
							Root[FPSTR(CONFIGKEY_PowerSaving)] = FL(POWER_SAVING_MODEM);
							break;
						case WIFI_LIGHT_SLEEP:
							Root[FPSTR(CONFIGKEY_PowerSaving)] = FL(POWER_SAVING_LIGHT);
							break;
					}
					Root[FPSTR(CONFIGKEY_WiFi_Power)] = AppConfig.WiFi_Power;
					Root[FPSTR(CONFIGKEY_WLAN_AP_Name)] = AppConfig.WLAN_AP_Name;
					Root[FPSTR(CONFIGKEY_WLAN_AP_Pass)] = AppConfig.WLAN_AP_Pass;
					Root[FPSTR(CONFIGKEY_WLAN_WPS)] = AppConfig.WLAN_WPS;
					Root[FPSTR(CONFIGKEY_Init_Retry_Count)] = AppConfig.Init_Retry_Count;
					Root[FPSTR(CONFIGKEY_Init_Retry_Cycle)] = AppConfig.Init_Retry_Cycle;
					Root[FPSTR(CONFIGKEY_Hostname)] = AppConfig.Hostname;
					Root[FPSTR(CONFIGKEY_Portal_Timeout)] = AppConfig.Portal_Timeout;
					Root[FPSTR(CONFIGKEY_Portal_APTest)] = AppConfig.Portal_APTest;
					Root[FPSTR(CONFIGKEY_NTP_Server)] = AppConfig.NTP_Server;
					{
						TimeChangeRule TZR, TZD;
						AppConfig.TZ.getRules(TZD, TZR);
						{
							JsonObject &TZRObj = Root.createNestedObject(FPSTR(CONFIGKEY_TimeZone_Regular));
							TZRObj[FPSTR(CONFIGKEY_TZ_Name)] = TZR.abbrev;
							TZRObj[FPSTR(CONFIGKEY_TZ_Week)] = TZR.week;
							TZRObj[FPSTR(CONFIGKEY_TZ_DayOfWeek)] = TZR.dow;
							TZRObj[FPSTR(CONFIGKEY_TZ_Month)] = TZR.month;
							TZRObj[FPSTR(CONFIGKEY_TZ_Hour)] = TZR.hour;
							TZRObj[FPSTR(CONFIGKEY_TZ_Offset)] = TZR.offset;
						}
						if (memcmp(&TZR, &TZD, sizeof(TimeChangeRule))) {
							JsonObject &TZDObj = Root.createNestedObject(FPSTR(CONFIGKEY_TimeZone_Daylight));
							TZDObj[FPSTR(CONFIGKEY_TZ_Name)] = TZD.abbrev;
							TZDObj[FPSTR(CONFIGKEY_TZ_Week)] = TZD.week;
							TZDObj[FPSTR(CONFIGKEY_TZ_DayOfWeek)] = TZD.dow;
							TZDObj[FPSTR(CONFIGKEY_TZ_Month)] = TZD.month;
							TZDObj[FPSTR(CONFIGKEY_TZ_Hour)] = TZD.hour;
							TZDObj[FPSTR(CONFIGKEY_TZ_Offset)] = TZD.offset;
						}
					}

					request.send(response);
				});
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->addHandler(
					new AsyncAPIAPScanWebHandler(FL(PORTAL_API_HWCTL_APSCAN))
				);
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->addHandler(
					new AsyncAPIConfigWebHandler(FL(PORTAL_API_CONFIG),
						get_dir(FL(CONFIG_DIR)))
				);
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->addHandler(
					new AsyncAPIOTAWebHandler(FL(PORTAL_API_OTA),
						FL(PORTAL_ROOT PORTAL_PAGE_OTA))
				);
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->serveStatic(FL(PORTAL_FSDAV_ROOT),
					VFATFS.openDir(FL("/")), String::EMPTY, FL(DEFAULT_CACHE_CTRL),
					true, true);
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}
			}

			{
				auto &Handler = AppGlobal.webServer->serveStatic(FL(PORTAL_ROOT),
					get_dir(FL(PORTAL_DIR)), FL(PORTAL_PAGE_INDEX), FL(DEFAULT_CACHE_CTRL));
				if (isCaptive) {
					Handler.addFilter([](AsyncWebRequest const &request) {
						return request.host().equalsIgnoreCase(AppConfig.Hostname);
					});
				}

				Handler._onGETIndexNotFound = [](AsyncWebRequest &request) {
					if (request.url() == FL(PORTAL_ROOT)) {
						Portal_WebServer_RespondBuiltInData(request,
							PORTAL_RESDATA_INDEX_HTML, FL(PORTAL_PAGE_INDEX));
					} else {
						// Do not allow directory listing
						request.send(403);
					}
				};

				Handler._onGETPathNotFound = [](AsyncWebRequest &request) {
					auto ResEntry = PortalStaticResMap->get_if([&](StaticResDefaults const &X) {
						ESPAPP_DEBUGVV("* Matching '%s' with built-in data '%s'...\n",
							request.url().c_str(), SFPSTR(X.Path));
						return (request.url() == FPSTR(X.Path));
					});
					if (ResEntry) {
						Portal_WebServer_RespondBuiltInData(request,
							ResEntry->Content, pathGetEntryName(request.url()));
					} else request.send(404);
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
			ESPAPP_LOG("ERROR: Unrecognised portal step (%d)\n",
				(int)AppGlobal.wsSteps);
			panic();
		}
	}
}

static void Portal_TimeAPTest() {
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

static bool Portal_DNS_IsSingleQuery(DNSHeader *header) {
	return ntohs(header->QDCount) == 1 &&
		header->ANCount == 0 &&
		header->NSCount == 0 &&
		header->ARCount == 0;
}

static String Portal_DNS_ParseQueryName(DNSHeader *header) {
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

static void Portal_DNS_SendReply(DNSHeader *header, AsyncUDPPacket &packet) {
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

static void Portal_Start() {
	SwitchState(APP_PORTAL);
	if (AppConfig.PersistWLAN) {
		WiFi.persistent(false);
	}
	if (!WiFi.softAPConfig(WLAN_PORTAL_IP, WLAN_PORTAL_GATEWAY, WLAN_PORTAL_SUBNET)) {
		ESPAPP_LOG("ERROR: Failed to configure WiFi base station!\n");
		panic();
	}
	if (!WiFi.softAP(AppConfig.Hostname.c_str())) {
		ESPAPP_LOG("ERROR: Failed to start WiFi base station!\n");
		panic();
	}
	ESPAPP_LOG("WiFi base station '%s' enabled!\n", AppConfig.Hostname.c_str());

	AppGlobal.portal.dnsServer = new AsyncUDP();
	if (AppGlobal.portal.dnsServer->listen(53)) {
		AppGlobal.portal.dnsServer->onPacket([](AsyncUDPPacket &packet) {
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

static void Portal_Stop() {
	if (AppGlobal.webServer) {
		AppGlobal.webServer->end();
		while (!AppGlobal.webServer->hasFinished()) {
			delay(100);
			if (AppGlobal.State == APP_PORTAL)
				__userapp_prestart_loop();
		}

		delete PortalStaticResMap;
		PortalStaticResMap = nullptr;
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

static void Portal_APTest() {
	ESPAPP_DEBUG("Probing WiFi access point '%s'...\n", AppConfig.WLAN_AP_Name.c_str());
	WiFi.begin(AppConfig.WLAN_AP_Name.c_str(), AppConfig.WLAN_AP_Pass.c_str());
	bool Connected = false;
	time_t startTS = GetCurrentTS();
	do {
		// Fast exit if AP scan is in progress
		if (APScanInProgress) {
			WiFi.disconnect(false);
			return;
		}

		delay(100);
		// We are taking a long time off the main loop, need to invoke user app loop separately
		__userapp_prestart_loop();
		if (APConnected) {
			ESPAPP_LOG("Successfully connected to WiFi access point '%s'!\n",
				AppConfig.WLAN_AP_Name.c_str());
			Connected = true;
			startTS = 0;
		}
	} while (GetCurrentTS() - startTS < AppConfig.Init_Retry_Cycle);

	if (Connected) {
		// Turn off AP
		if (!WiFi.enableAP(false)) {
			ESPAPP_LOG("ERROR: Failed to disable WiFi base station mode!\n");
			panic();
		}
		ESPAPP_LOG("WiFi base station '%s' disabled!\n", AppConfig.Hostname.c_str());
		// Retry init stage again
		Init_Start();
	} else {
		// Turn off STA
		WiFi.disconnect(true);
		ESPAPP_DEBUG("Unable to connect to WiFi access point '%s'\n",
			AppConfig.WLAN_AP_Name.c_str());
	}
}

static void loop_PORTAL() {
	Portal_WebServer_Operations();
	if (AppGlobal.portal.performAPTest) {
		Portal_APTest();
		AppGlobal.portal.performAPTest = false;
	}
}

static void perform_DevReset() {
	ESPAPP_DEBUG("Formatting file system...\n");
	if (!VFATFS.format()) {
		ESPAPP_DEBUG("ERROR: Failed to format file system!\n");
		panic();
	}
	SwitchState(APP_DEVRESTART);
}

static void perform_DevRestart() {
	ESPAPP_DEBUG("Unmounting file system...\n");
	VFATFS.end();
	ESPAPP_DEBUG("Saving clock to RTC...\n");
	updateRTCClock();
	ESPAPP_LOG("Restarting device...\n");
	ESP.restart();
	// Should not reach!
	panic();
}

static void Service_TimePortal() {
	unsigned int PortalIdle = GetCurrentTS() - AppGlobal.wsActivityTS;
	if (PortalIdle >= AppConfig.Portal_Timeout) {
		AppGlobal.service.portalTimer->detach();
		if (!AppGlobal.NoService) {
			ESPAPP_DEBUG("Portal idle for %s, shutting down...\n",
				ToString(PortalIdle, TimeUnit::SEC, true).c_str());
			AppGlobal.wsSteps = PORTAL_DOWN;
		}
	}
}

static void Service_StartPortal(time_t StartTS) {
	if (AppConfig.Portal_Timeout) {
		if (!AppGlobal.service.portalTimer)
			AppGlobal.service.portalTimer = new Ticker();
		AppGlobal.service.portalTimer->attach(10, Service_TimePortal);
	}
	AppGlobal.wsActivityTS = StartTS;
	AppGlobal.wsSteps = PORTAL_SETUP;
}

static void Service_APMonitor() {
	time_t Now = GetCurrentTS();
	if (RTCFlags & RTC_FLAG_BOOTFAIL) {
		if (Now - AppGlobal.StageTS > TRIVIAL_FAILURE_DELAY) {
			ESPAPP_DEBUGV("Service seems stable enough, removing trivial failure flag...\n");
			RTCFlags &= ~RTC_FLAG_BOOTFAIL;
			RTCMemory &RTCMem = RTCMemory::Manager();
			if (!RTCMem.Write(RTC_SLOT_FLAGS, &RTCFlags, 1)) {
				ESPAPP_DEBUG("WARNING: Failed to update appliance flags to RTC\n");
			}
		}
	}

	if (!APScanInProgress) {
		// Only check WiFi connection status when there is no AP scan
		if (APConnected) {
			if (AppGlobal.service.lastAPAvailableTS) {
				AppGlobal.service.lastAPAvailableTS = 0;
				ESPAPP_DEBUG("Re-connected to WiFi access point!\n");
			}
		} else {
			if (!AppGlobal.service.lastAPAvailableTS) {
				AppGlobal.service.lastAPAvailableTS = Now;
				ESPAPP_DEBUG("Disconnected from WiFi access point, "
					"waiting for reconnect...\n");
			} else {
				time_t BreakSpan = Now - AppGlobal.service.lastAPAvailableTS;
				if (BreakSpan >= AppConfig.Init_Retry_Count * AppConfig.Init_Retry_Cycle) {
					ESPAPP_DEBUG("Unable to reconnect to WiFi access point, "
						"stopping service...\n");
					AppGlobal.service.portalFallback = true;
				} else if (BreakSpan && !(BreakSpan % AppConfig.Init_Retry_Cycle)) {
					ESPAPP_DEBUGV("Disconnected from WiFi access point for %s\n",
						ToString(BreakSpan, TimeUnit::SEC, true).c_str());
					WiFi.reconnect();
				}
			}
		}
	}
}

static void Service_Start() {
	SwitchState(APP_SERVICE);

	AppGlobal.service.apTestTimer = new Ticker();
	AppGlobal.service.apTestTimer->attach(1, Service_APMonitor);
	if (AppConfig.Portal_Timeout || AppGlobal.NoService) {
		Service_StartPortal(AppGlobal.StageTS);
	}

	if (!AppGlobal.NoService) {
		if (!__userapp_startup()) {
			// Fall back to service bypass mode
			AppGlobal.NoService = true;
			return;
		}
	}
}

static void loop_SERVICE() {
	if (AppGlobal.service.portalFallback) {
		// Pause mode switching if AP scan is in progress
		if (APScanInProgress) return;

		if (AppConfig.PersistWLAN) {
			WiFi.persistent(false);
		}
		// Turn off STA
		WiFi.disconnect(true);
		// Forget that we had an IP
		APReceivedIP = false;
		delay(100);
		Portal_Start();
		return;
	}

	if (!AppGlobal.NoService) {
		if (AppGlobal.service.reload) {
			AppGlobal.service.reload = false;
			__userapp_teardown();
			if (!__userapp_startup()) {
				// Fall back to service bypass mode
				AppGlobal.NoService = true;
				return;
			}
		}
		__userapp_loop();
	} else {
		if (AppGlobal.wsSteps == PORTAL_OFF) {
			AppGlobal.wsSteps = PORTAL_SETUP;
		}
	}

	Portal_WebServer_Operations();
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
			ESPAPP_LOG("ERROR: Unrecognised state (%d)\n", (int)AppGlobal.State);
			panic();
	}
	if (AppGlobal.State < APP_SERVICE) {
		__userapp_prestart_loop();
	}
}

// User-App service routines

Dir Appliance_GetDir(String const &path) {
	return get_dir(path);
}

time_t Appliance_CurrentTS() {
	return GetCurrentTS();
}

time_t Appliance_UTCTimeofDay(struct tm *tm_out) {
	time_t Ret = sntp_get_current_timestamp();
	if (tm_out) localtime_r(&Ret, tm_out);
	return Ret;
}

time_t Appliance_LocalTimeofDay(struct tm *tm_out, TimeChangeRule **tcr) {
	time_t Ret = sntp_get_current_timestamp();
	TimeChangeRule* TZ;
	Ret = AppConfig.TZ.toLocal(Ret, &TZ);
	if (tcr) *tcr = TZ;
	if (tm_out) localtime_r(&Ret, tm_out);
	return Ret;
}

JsonManagerResults Appliance_LoadConfig(String const &filename,
	std::function<void(JsonObject const &obj)> const &callback) {
	return load_config(filename, callback);
}

JsonManagerResults Appliance_UpdateConfig(String const &filename,
	JsonObjectCallback const &callback) {
	return update_config(filename, callback);
}

bool Appliance_RTCMemory_isRestored() {
	return RTCFlags & RTC_FLAG_RESTORED;
}

uint8_t Appliance_RTCMemory_Available() {
	return RTCMEMORY_USERSLOTS - RTC_SLOTS_RESERVED;
}

bool Appliance_RTCMemory_Read(uint8_t offset, uint32_t *buf, uint8_t count) {
	RTCMemory &RTCMem = RTCMemory::Manager();
	return RTCMem.Read(RTC_SLOTS_RESERVED+offset, buf, count);
}

bool Appliance_RTCMemory_Write(uint8_t offset, uint32_t *buf, uint8_t count) {
	RTCMemory &RTCMem = RTCMemory::Manager();
	return RTCMem.Write(RTC_SLOTS_RESERVED+offset, buf, count);
}

AsyncWebServer* Appliance_WebPortal() {
	return AppGlobal.webServer;
}

void Appliance_WebPortal_TimedStart() {
	if (AppGlobal.wsSteps == PORTAL_OFF) {
		Service_StartPortal(GetCurrentTS());
	}
}

void Appliance_WebPortal_Stop() {
	if (AppGlobal.wsSteps > PORTAL_DOWN) {
		AppGlobal.wsSteps = PORTAL_DOWN;
	}
}

void Appliance_WebPortal_RegisterStaticResDefault(PGM_P path, PGM_P content) {
	PortalStaticResMap->append({path, content});
}

void Appliance_WebPortal_RespondBuiltInData(AsyncWebRequest &request,
	PGM_P data, String const &filename, int code) {
	return Portal_WebServer_RespondBuiltInData(request, data, filename, code);
}

void Appliance_WebPortal_RespondFileOrBuiltIn(AsyncWebRequest &request,
	String const &filename, PGM_P defdata, int code) {
	return Portal_WebServer_RespondFileOrBuiltIn(request, filename, defdata, code);
}

File Appliance_WebPortal_CheckRestoreRes(Dir &dir, String const &resfile,
	PGM_P resdata) {
	return Portal_WebServer_CheckRestoreRes(dir, resfile, resdata);
}

void Appliance_Service_Reload() {
	AppGlobal.service.reload = true;
}

void Appliance_Device_Restart() {
	SwitchState(APP_DEVRESTART);
}

static void APScan_Finished(bss_info* result, STATUS status) {
	Portal_StopAPScan();
	if (status != OK) {
		ESPAPP_DEBUGV("AP scan failed!\n");
		return;
	}

	ESPAPP_DEBUGV("AP scan completed!\n");
	APList.clear();
	APScanLast = GetCurrentTS();

	bss_info* scanEntry = result;
	ESPAPP_DEBUGDO(int8_t apCnt = 0; int8_t apNCnt = 0);
	while (scanEntry) {
		APEntry entry;
		ESPAPP_DEBUGDO(++apCnt);
		if (!scanEntry->is_hidden) {
			entry.SSID = String((char*)scanEntry->ssid, scanEntry->ssid_len);
			ESPAPP_DEBUGDO(++apNCnt);
		}
		memcpy(entry.MAC, scanEntry->bssid, 6);
		entry.Channel = scanEntry->channel;
		entry.RSSI = scanEntry->rssi;
		entry.Features = 0;
		if (scanEntry->phy_11b) entry.Features |= AP_PHY_11b;
		if (scanEntry->phy_11g) entry.Features |= AP_PHY_11g;
		if (scanEntry->phy_11n) entry.Features |= AP_PHY_11n;
		if (scanEntry->wps) entry.Features |= AP_WPS;
		entry.Auth = scanEntry->authmode;
		APList.append(std::move(entry));

		ESPAPP_DEBUGVVDO({
			String APEntry = PrintMAC(scanEntry->bssid);
			if (!scanEntry->is_hidden) {
				APEntry.concat(' ');
				APEntry.concat('[');
				APEntry.concat((char*)scanEntry->ssid, scanEntry->ssid_len);
				APEntry.concat(']');
			}
			APEntry.concat(FL(" CH"));
			APEntry.concat(scanEntry->channel);
			APEntry.concat(',');
			APEntry.concat(scanEntry->rssi);
			APEntry.concat(FL("dBm "));
			APEntry.concat('(');
			if (scanEntry->phy_11b) APEntry.concat('b');
			if (scanEntry->phy_11g) APEntry.concat('g');
			if (scanEntry->phy_11n) APEntry.concat('n');
			if (scanEntry->wps) APEntry.concat(FL(",WPS"));
			APEntry.concat(')');
			APEntry.concat(' ');
			APEntry.concat(PrintAuth(scanEntry->authmode));
			ESPAPP_LOG("AP #%d: %s\n",apCnt,APEntry.c_str());
		});
		scanEntry = STAILQ_NEXT(scanEntry, next);
	}
	ESPAPP_DEBUG("* Discovered %d APs (%d named)\n", apCnt, apNCnt);
}

bool Appliance_APScan_Start(wifi_scan_type_t ScanType, time_t FreshDur) {
	if (APScanInProgress) {
		ESPAPP_DEBUG("WARNING: AP scan already in progress...\n");
		return true;
	}
	if (!APList.isEmpty()) {
		time_t curTS = GetCurrentTS();
		if (curTS - APScanLast < FreshDur) {
			ESPAPP_DEBUGV("Existing AP scan result still fresh\n");
			return true;
		}
	}
	if (!Portal_StartAPScan()) {
		ESPAPP_DEBUG("Current state does not allow AP scanning\n");
		return false;
	}
	if (!WiFi.enableSTA(true)) {
		ESPAPP_DEBUG("Failed to enable WiFi client mode for AP scan\n");
		return false;
	}
	scan_config config = {0};
	config.show_hidden = true;
	config.scan_type = ScanType;
	if (!wifi_station_scan(&config, (scan_done_cb_t)APScan_Finished)) {
		ESPAPP_DEBUG("Failed to start AP scan\n");
		return false;
	}
	return true;
}

bool Appliance_APScan_InProgress() {
	return APScanInProgress;
}

void Appliance_EnumAPList(TAPList::Predicate const &pred) {
	APList.apply([&](APEntry &i) { return pred(i); });
}

void Appliance_ClearAPList() {
	APList.clear();
}