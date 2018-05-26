#ifndef __PGMRES__
#define __PGMRES__

#define ZWAPP_VERSION   "0.2"

#define CONFIG_DIR            "/config"
#define APPLIANCE_CONFIG_FILE "appliance.json"
#define PORTAL_ACCOUNTS_FILE  "portal.accounts.txt"
#define PORTAL_ACCESS_FILE    "portal.access.txt"

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

#define PORTAL_API_HWCTL              PORTAL_API_ROOT "hwctl/"
#define PORTAL_API_HWCTL_DEVRESET     PORTAL_API_HWCTL "reset"
#define PORTAL_API_HWCTL_DEVRESTART   PORTAL_API_HWCTL "restart"

#define PORTAL_API_HWMON        PORTAL_API_ROOT "hwmon/"
#define PORTAL_API_HWMON_HEAP   PORTAL_API_HWMON "heap"
#define PORTAL_API_HWMON_UPTIME PORTAL_API_HWMON "uptime"

#define PORTAL_API_APSCAN 	    PORTAL_API_ROOT "apscan"
#define PORTAL_API_CLOCK        PORTAL_API_ROOT "clock"
#define PORTAL_API_CONFIG       PORTAL_API_ROOT "config/"
#define PORTAL_API_AUTH         PORTAL_API_ROOT "auth/"
#define PORTAL_API_OTA          PORTAL_API_ROOT "ota"

#define PORTAL_FSDAV            PORTAL_ROOT "~"
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

#endif //__PGMRES__
