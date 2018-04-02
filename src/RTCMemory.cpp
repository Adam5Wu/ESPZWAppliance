
#include "RTCMemory.hpp"

RTCMemory::RTCMemory() {
#if !RTCMEMORY_FULLBUFFER
	RTCDATA _rtcData;
#endif
	ESPAPPRTCM_DEBUGVV("Loading RTC memory...\n");
	ESP.rtcUserMemoryRead(0, (uint32_t*)&_rtcData, RTCMEMORY_MAXLEN);
	uint8_t sig[MD5_BINLEN];
	calcMD5(_rtcData.data, sizeof(_rtcData.data), sig);
	if (memcmp(sig, _rtcData.sig, MD5_BINLEN) != 0) {
		ESPAPPRTCM_DEBUG("RTC memory signature invalid\n");
		memset(_rtcData.data, 0, sizeof(_rtcData.data));
		calcMD5(_rtcData.data, sizeof(_rtcData.data), _rtcData.sig);
		ESPAPPRTCM_DEBUGV("Initializing RTC memory...\n");
		if (!ESP.rtcUserMemoryWrite(0, (uint32_t*)&_rtcData, RTCMEMORY_MAXLEN)) {
			ESPAPPRTCM_DEBUG("ERROR: Failed to initialize RTC memory\n");
			panic();
		}
	} else {
		ESPAPPRTCM_DEBUGVV("RTC memory signature valid\n");
	}
}

bool RTCMemory::Read(uint8_t offset, uint32_t *buf, uint8_t count) {
#if RTCMEMORY_FULLBUFFER
	if (offset+count > RTCMEMORY_USERSLOTS) {
		ESPAPPRTCM_DEBUG("WARNING: Reject out-of-bound RTC memory read\n");
		return false;
	}
	memcpy(buf, _rtcData.data+offset, count*4);
	return true;
#else
	return ESP.rtcUserMemoryRead(RTCMEMORY_USERSTART+offset, buf, count*4);
#endif
}

bool RTCMemory::Write(uint8_t offset, uint32_t const *buf, uint8_t count) {
#if RTCMEMORY_FULLBUFFER
	if (offset+count > RTCMEMORY_USERSLOTS) {
		ESPAPPRTCM_DEBUG("WARNING: Reject out-of-bound RTC memory write\n");
		return false;
	}
#else
	RTCDATA _rtcData;
	ESPAPPRTCM_DEBUGVV("Loading RTC memory...\n");
	ESP.rtcUserMemoryRead(RTCMEMORY_USERSTART, _rtcData.data, RTCMEMORY_USERSLOTS*4);
#endif
	memcpy(_rtcData.data+offset, buf, count*4);
	ESPAPPRTCM_DEBUGVV("Recalculating RTC memory signature...\n");
	calcMD5(_rtcData.data, sizeof(_rtcData.data), _rtcData.sig);

#if RTCMEMORY_FULLUPDATE
	ESPAPPRTCM_DEBUGVV("Updating RTC memory data...\n");
	if (!ESP.rtcUserMemoryWrite(RTCMEMORY_USERSTART+offset, buf, count*4)) {
		ESPAPPRTCM_DEBUG("WARNING: Failed to update RTC memory data\n");
		return false;
	}
	ESPAPPRTCM_DEBUGVV("Updating RTC memory signature...\n");
	if (!ESP.rtcUserMemoryWrite(0, _rtcData.sig, MD5_BINLEN)) {
		ESPAPPRTCM_DEBUG("ERROR: Failed to update RTC memory signature\n");
		panic();
	}
#else
	ESPAPPRTCM_DEBUGVV("Updating RTC memory...\n");
	if (!ESP.rtcUserMemoryWrite(0, (uint32_t*)&_rtcData, RTCMEMORY_MAXLEN)) {
		ESPAPPRTCM_DEBUG("ERROR: Failed to update RTC memory\n");
		panic();
	}
#endif
	return true;
}

RTCMemory & RTCMemory::Manager() {
	static RTCMemory __IoFU; // Initialize on first use
	return __IoFU;
}
