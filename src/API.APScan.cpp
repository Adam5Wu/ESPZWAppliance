
#include "API.APScan.hpp"

#include <ESP8266WiFi.h>

#include <Units.h>
#include <LinkedList.h>

#include <AsyncJsonResponse.h>

extern "C" {
	#include "user_interface.h"
}

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

static LinkedList<APEntry> APList(nullptr);
static time_t LastScanTS = 0;
static bool ScanInProg = false;

extern time_t GetCurrentTS();
extern bool Portal_StartAPScan();
extern void Portal_StopAPScan();

void ScanFinished(bss_info* result, STATUS status) {
	Portal_StopAPScan();
	ScanInProg = false;
	if (status != OK) {
		ESPWSAPSCAN_DEBUGV("AP scan failed!\n");
		return;
	}

	ESPWSAPSCAN_DEBUGV("AP scan completed!\n");
	LastScanTS = GetCurrentTS();
	APList.clear();

	bss_info* scanEntry = result;
	ESPWSAPSCAN_DEBUGDO(int8_t apCnt = 0; int8_t apNCnt = 0);
	while (scanEntry) {
		APEntry entry;
		ESPWSAPSCAN_DEBUGDO(++apCnt);
		if (!scanEntry->is_hidden) {
			entry.SSID = String((char*)scanEntry->ssid, scanEntry->ssid_len);
			ESPWSAPSCAN_DEBUGDO(++apNCnt);
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

		ESPWSAPSCAN_DEBUGDO({
			String APEntry;
			for (int i = 0; i < 6; i++) {
				if (APEntry) APEntry.concat(':');
				APEntry.concat(HexLookup_UC[scanEntry->bssid[i] >> 4 & 0xF]);
				APEntry.concat(HexLookup_UC[scanEntry->bssid[i] & 0xF]);
			}
			if (!scanEntry->is_hidden) {
				APEntry.concat(' ');
				APEntry.concat('[');
				APEntry.concat((char*)scanEntry->ssid, scanEntry->ssid_len);
				APEntry.concat(']');
			}
			APEntry.concat(F(" CH"));
			APEntry.concat(scanEntry->channel);
			APEntry.concat(',');
			APEntry.concat(scanEntry->rssi);
			APEntry.concat(FC("dBm "));
			APEntry.concat('(');
			if (scanEntry->phy_11b) APEntry.concat('b');
			if (scanEntry->phy_11g) APEntry.concat('g');
			if (scanEntry->phy_11n) APEntry.concat('n');
			if (scanEntry->wps) APEntry.concat(F(",WPS"));
			APEntry.concat(')');
			APEntry.concat(' ');
			switch (scanEntry->authmode) {
				case AUTH_OPEN: APEntry.concat(F("OPEN")); break;
				case AUTH_WEP: APEntry.concat(F("WEP")); break;
				case AUTH_WPA_PSK: APEntry.concat(F("WPA-PSK")); break;
				case AUTH_WPA2_PSK: APEntry.concat(F("WPA2-PSK")); break;
				case AUTH_WPA_WPA2_PSK: APEntry.concat(F("WPA/WPA2-PSK")); break;
				default: APEntry.concat(F("UNSUPPORTED")); break;
			}
			ESPWSAPSCAN_LOG("AP #%d: %s\n",apCnt,APEntry.c_str());
		});
		scanEntry = STAILQ_NEXT(scanEntry, next);
	}
	ESPWSAPSCAN_DEBUG("* Discovered %d APs (%d named)\n", apCnt, apNCnt);
}

bool AsyncAPIAPScanWebHandler::_canHandle(AsyncWebRequest const &request) {
	if (!(HTTP_GET & request.method())) return false;
	return request.url() == Path;
}

bool AsyncAPIAPScanWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	// Validate query parameter
	size_t qlen = request.queries();
	if (qlen > 1) {
		ESPWSAPSCAN_DEBUGV("[%s] Received %d queries, expect <= 1\n",
			request._remoteIdent.c_str(), qlen);
		request.send_P(400, PSTR("Invalid query count (expect <=1)"), F("text/plain"));
		return false;
	}
	if (qlen) {
		bool Understood = false;
		request.enumQueries([&](const AsyncWebQuery &Q) {
			if (Q.name == FC("force")) {
				ForceScan = true;
				if (Q.value) {
					ESPWSAPSCAN_DEBUGV("[%s] Unexpected query value %s='%s'\n",
						request._remoteIdent.c_str(), Q.name.c_str(), Q.value.c_str());
				} else Understood = true;
			} else {
				ESPWSAPSCAN_DEBUGV("[%s] Unexpected query key '%s'\n",
					request._remoteIdent.c_str(), Q.name.c_str());
			}
			return true;
		});
		if (!Understood) {
			request.send_P(400, PSTR("Invalid query parameter"), F("text/plain"));
			return false;
		}
	}
	return AsyncWebHandler::_checkContinue(request, continueHeader);
}

void AsyncAPIAPScanWebHandler::_handleRequest(AsyncWebRequest &request) {
	if (ScanInProg) {
		ESPWSAPSCAN_DEBUGV("[%s] Previous scan in progress...\n",
			request._remoteIdent.c_str());
		request.send(204);
		return;
	}
	if (ForceScan || APList.isEmpty()) {
		if (!Portal_StartAPScan()) {
			ESPWSAPSCAN_DEBUGV("[%s] Current state does not allow scanning\n",
				request._remoteIdent.c_str());
			request.send_P(503, PSTR("Function temporarily unavailable"), F("text/plain"));
			return;
		}
		if (!WiFi.enableSTA(true)) {
			ESPWSAPSCAN_DEBUGV("[%s] Failed to enable WiFi client mode!\n",
				request._remoteIdent.c_str());
			request.send_P(500, PSTR("Unable to enter scan mode"), F("text/plain"));
			return;
		}
		ScanInProg = true;
		scan_config config = {0};
		config.show_hidden = true;
		config.scan_type = WIFI_SCAN_TYPE_PASSIVE;
		if (!wifi_station_scan(&config, (scan_done_cb_t)ScanFinished)) {
			ESPWSAPSCAN_DEBUGV("[%s] Failed to start AP scan\n", request._remoteIdent.c_str());
			request.send(204);
		} else {
			ESPWSAPSCAN_DEBUGV("[%s] Starting AP scan...\n", request._remoteIdent.c_str());
			request.send(204);
		}
		return;
	}

	AsyncJsonResponse * response =
		AsyncJsonResponse::CreateNewObjectResponse();
	JsonObject& Root = response->root.as<JsonObject>();
	Root[FC("update")] = LastScanTS;
	JsonArray& APs = Root.createNestedArray(FC("APs"));

	for (auto iter = APList.begin(); iter != APList.end(); ++iter) {
		JsonObject& AP = APs.createNestedObject();
		if (iter->SSID) AP[FC("SSID")] = iter->SSID;
		{
			String MACString;
			for (int i = 0; i < 6; i++) {
				MACString.concat(HexLookup_UC[iter->MAC[i] >> 4 & 0xF]);
				MACString.concat(HexLookup_UC[iter->MAC[i] & 0xF]);
			}
			AP[FC("MAC")] = MACString;
		}
		{
			JsonArray& RF = AP.createNestedArray(FC("RF"));
			RF.add(iter->Channel);
			RF.add(iter->RSSI);
			String PHYs;
			if (iter->Features & AP_PHY_11b) PHYs.concat('b');
			if (iter->Features & AP_PHY_11g) PHYs.concat('g');
			if (iter->Features & AP_PHY_11n) PHYs.concat('n');
			RF.add(PHYs);
		}
		{
			JsonArray& Auth = AP.createNestedArray(FC("Auth"));
			switch (iter->Auth) {
				case AUTH_OPEN: Auth.add(F("OPEN")); break;
				case AUTH_WEP: Auth.add(F("WEP")); break;
				case AUTH_WPA_PSK: Auth.add(F("WPA-PSK")); break;
				case AUTH_WPA2_PSK: Auth.add(F("WPA2-PSK")); break;
				case AUTH_WPA_WPA2_PSK: Auth.add(F("WPA/WPA2-PSK")); break;
				default: Auth.add(F("UNSUPPORTED")); break;
			}
			if (iter->Features & AP_WPS) Auth.add(F("WPS"));
		}
	}
	ESPWSAPSCAN_DEBUGVDO(response->setPrettyPrint());
	request.send(response);
}
