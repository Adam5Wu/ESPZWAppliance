
#include "API.APScan.hpp"

#include <ESPZWAppliance.h>

#include <AsyncJsonResponse.h>

bool AsyncAPIAPScanWebHandler::_canHandle(AsyncWebRequest const &request) {
	if (!(HTTP_GET & request.method())) return false;
	return request.url() == Path;
}

bool AsyncAPIAPScanWebHandler::_checkContinue(AsyncWebRequest &request, bool continueHeader) {
	// Validate query parameter
	size_t qlen = request.queries();
	if (qlen > 1) {
		ESPWSAPSCAN_DEBUG("[%s] Received %d queries, expect <= 1\n",
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
					ESPWSAPSCAN_DEBUG("[%s] Unexpected query value %s='%s'\n",
						request._remoteIdent.c_str(), Q.name.c_str(), Q.value.c_str());
				} else Understood = true;
			} else {
				ESPWSAPSCAN_DEBUG("[%s] Unexpected query key '%s'\n",
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
	if (Appliance_APScan_InProgress()) {
		ESPWSAPSCAN_DEBUG("[%s] Previous scan in progress...\n",
			request._remoteIdent.c_str());
		request.send(204);
		return;
	}
	if (!Appliance_APScan_Start(WIFI_SCAN_TYPE_PASSIVE,
		ForceScan ? 0 : APSCAN_FRESH_DURATION_DEFAULT)) {
		ESPWSAPSCAN_DEBUGV("[%s] Unable to start AP scan\n",
			request._remoteIdent.c_str());
		request.send_P(503, PSTR("Unable to start AP scan, try again later."),
			F("text/plain"));
		return;
	}
	ForceScan = false;
	if (Appliance_APScan_InProgress()) {
		ESPWSAPSCAN_DEBUG("[%s] New scan in progress...\n",
			request._remoteIdent.c_str());
		request.send(204);
		return;
	}

	// Need dynamic buffer support!
	AsyncJsonResponse *response = AsyncJsonResponse::CreateNewArrayResponse(200, 4096);
	JsonArray& APs = response->root.as<JsonArray&>();

	Appliance_EnumAPList([&](APEntry const &entry) {
		JsonObject& AP = APs.createNestedObject();
		if (entry.SSID) AP[FC("SSID")] = entry.SSID;
		{
			String MACString;
			for (int i = 0; i < 6; i++) {
				MACString.concat(HexLookup_UC[entry.MAC[i] >> 4 & 0xF]);
				MACString.concat(HexLookup_UC[entry.MAC[i] & 0xF]);
			}
			AP[FC("MAC")] = MACString;
		}
		{
			JsonArray& RF = AP.createNestedArray(FC("RF"));
			RF.add(entry.Channel);
			RF.add(entry.RSSI);
			String PHYs;
			if (entry.Features & AP_PHY_11b) PHYs.concat('b');
			if (entry.Features & AP_PHY_11g) PHYs.concat('g');
			if (entry.Features & AP_PHY_11n) PHYs.concat('n');
			RF.add(PHYs);
		}
		{
			JsonArray& Auth = AP.createNestedArray(FC("Auth"));
			switch (entry.Auth) {
				case AUTH_OPEN: Auth.add(F("OPEN")); break;
				case AUTH_WEP: Auth.add(F("WEP")); break;
				case AUTH_WPA_PSK: Auth.add(F("WPA-PSK")); break;
				case AUTH_WPA2_PSK: Auth.add(F("WPA2-PSK")); break;
				case AUTH_WPA_WPA2_PSK: Auth.add(F("WPA/WPA2-PSK")); break;
				default: Auth.add(F("UNSUPPORTED")); break;
			}
			if (entry.Features & AP_WPS) Auth.add(F("WPS"));
		}
		return true;
	});

	ESPWSAPSCAN_DEBUGVDO(response->setPrettyPrint());
	request.send(response);
}
