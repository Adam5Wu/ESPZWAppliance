#ifndef __PGMRES__
#define __PGMRES__

#define ZWAPP_VERSION   "0.3"

#define CONFIG_DIR              "/config"
#define APPLIANCE_CONFIG_FILE   "appliance.json"
#define PORTAL_ACCOUNTS_FILE    "portal.accounts.txt"
#define PORTAL_ACCESS_FILE      "portal.access.txt"

#define PORTAL_DIR              "/portal"
#define PORTAL_HTML_EXT         ".html"
#define PORTAL_PAGE_INDEX       "index" PORTAL_HTML_EXT
#define PORTAL_PAGE_PANIC       "panic" PORTAL_HTML_EXT
#define PORTAL_PAGE_DEVRESET    "devreset" PORTAL_HTML_EXT
#define PORTAL_PAGE_DEVRESTART  "devrestart" PORTAL_HTML_EXT
#define PORTAL_PAGE_OTA         "ota" PORTAL_HTML_EXT
#define PORTAL_PAGE_SYSCONFIG   "sysconf" PORTAL_HTML_EXT
#define PORTAL_PAGE_AUTHCONFIG  "authconf" PORTAL_HTML_EXT

#define PORTAL_RES_MD5JS        "md5.js"
#define PORTAL_RES_JQUERYJS     "jquery.js"
#define PORTAL_RES_OTACOREJS    "ota-core.js"
#define PORTAL_RES_APSCANCOREJS "apscan-core.js"

#define PORTAL_ADMIN_USER   "Admin"

#define PORTAL_ROOT         "/"

#define PORTAL_WPAD_FILE    PORTAL_ROOT "wpad.dat"

#define PORTAL_API_ROOT     PORTAL_ROOT "api/"

#define PORTAL_API_HWCTL              PORTAL_API_ROOT  "hwctl/"
#define PORTAL_API_HWCTL_DEVRESET     PORTAL_API_HWCTL "reset"
#define PORTAL_API_HWCTL_DEVRESTART   PORTAL_API_HWCTL "restart"
#define PORTAL_API_HWCTL_APSCAN       PORTAL_API_HWCTL "apscan"

#define PORTAL_API_HWMON          PORTAL_API_ROOT  "hwmon/"
#define PORTAL_API_HWMON_HEAP     PORTAL_API_HWMON "heap"
#define PORTAL_API_HWMON_UPTIME   PORTAL_API_HWMON "uptime"

#define PORTAL_API_VERSION        PORTAL_API_ROOT "version/"
#define PORTAL_API_VERSION_ZWAPP  PORTAL_API_VERSION "zwapp"

#define PORTAL_API_STATE          PORTAL_API_ROOT "state/"
#define PORTAL_API_STATE_WLAN     PORTAL_API_STATE "wlan"
#define PORTAL_API_STATE_CLOCK    PORTAL_API_STATE "clock"

#define PORTAL_API_STATE_CONFIG         PORTAL_API_STATE "config/"
#define PORTAL_API_STATE_CONFIG_ZWAPP   PORTAL_API_STATE_CONFIG "zwapp"

#define PORTAL_API_CONFIG       PORTAL_API_ROOT "config/"
#define PORTAL_API_AUTH         PORTAL_API_ROOT "auth/"
#define PORTAL_API_OTA          PORTAL_API_ROOT "ota"

#define PORTAL_FSDAV            PORTAL_ROOT  "~"
#define PORTAL_FSDAV_ROOT       PORTAL_FSDAV "/"

#include <ESPEasyAuth.h>

const char PORTAL_DEFAULT_ACL[] PROGMEM =
  PORTAL_ROOT         ":$BR:"   ANONYMOUS_ID      "\n"
  PORTAL_API_ROOT     ":$BR:"   AUTHENTICATED_ID  "\n"
  PORTAL_API_HWCTL    ":GET:"   PORTAL_ADMIN_USER "\n"
  PORTAL_API_CONFIG   ":$B:"    PORTAL_ADMIN_USER "\n"
  PORTAL_API_AUTH     ":$B:"    PORTAL_ADMIN_USER "\n"
  PORTAL_API_OTA      ":$B:"    PORTAL_ADMIN_USER "\n"
  PORTAL_FSDAV_ROOT   ":$A:"    PORTAL_ADMIN_USER "\n";

extern const char PORTAL_RESDATA_INDEX_HTML[];
extern const char PORTAL_RESDATA_DEVRESET_HTML[];
extern const char PORTAL_RESDATA_DEVRESTART_HTML[];
extern const char PORTAL_RESDATA_PANIC_HTML[];
extern const char PORTAL_RESDATA_OTA_HTML[];
extern const char PORTAL_RESDATA_SYSCONFIG_HTML[];

extern const char PORTAL_RESDATA_MD5_JS[];
extern const char PORTAL_RESDATA_OTACORE_JS[];
extern const char PORTAL_RESDATA_JQUERY_JS[];
extern const char PORTAL_RESDATA_APSCANCORE_JS[];

const char CONFIGKEY_Production[] PROGMEM = "Production";
const char CONFIGKEY_PersistWLAN[] PROGMEM = "PersistWLAN";
const char CONFIGKEY_PowerSaving[] PROGMEM = "PowerSaving";
const char CONFIGKEY_WiFi_Power[] PROGMEM = "WiFi_Power";
const char CONFIGKEY_WLAN_AP_Name[] PROGMEM = "WLAN_AP_Name";
const char CONFIGKEY_WLAN_AP_Pass[] PROGMEM = "WLAN_AP_Pass";
const char CONFIGKEY_WLAN_WPS[] PROGMEM = "WLAN_WPS";
const char CONFIGKEY_Init_Retry_Count[] PROGMEM = "Init_Retry_Count";
const char CONFIGKEY_Init_Retry_Cycle[] PROGMEM = "Init_Retry_Cycle";
const char CONFIGKEY_Hostname[] PROGMEM = "Hostname";
const char CONFIGKEY_Portal_Timeout[] PROGMEM = "Portal_Timeout";
const char CONFIGKEY_Portal_APTest[] PROGMEM = "Portal_APTest";
const char CONFIGKEY_NTP_Server[] PROGMEM = "NTP_Server";
const char CONFIGKEY_TimeZone_Regular[] PROGMEM = "TimeZone_Regular";
const char CONFIGKEY_TimeZone_Daylight[] PROGMEM = "TimeZone_Daylight";
const char CONFIGKEY_TZ_Name[] PROGMEM = "Name";
const char CONFIGKEY_TZ_Week[] PROGMEM = "Week";
const char CONFIGKEY_TZ_DayOfWeek[] PROGMEM = "DayOfWeek";
const char CONFIGKEY_TZ_Month[] PROGMEM = "Month";
const char CONFIGKEY_TZ_Hour[] PROGMEM = "Hour";
const char CONFIGKEY_TZ_Offset[] PROGMEM = "Offset";


#endif //__PGMRES__
