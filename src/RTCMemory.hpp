#ifndef __RTCMEMORY_H__
#define __RTCMEMORY_H__

#include <eboot_command.h>

#include <Misc.h>

#include "AppBaseUtils.hpp"

#ifndef ESPAPPRTCM_DEBUG_LEVEL
#define ESPAPPRTCM_DEBUG_LEVEL ESPAPP_DEBUG_LEVEL
#endif

#ifndef ESPAPPRTCM_LOG
#define ESPAPPRTCM_LOG(...) ESPZW_LOG(__VA_ARGS__)
#endif

#if ESPAPPRTCM_DEBUG_LEVEL < 1
#define ESPAPPRTCM_DEBUGDO(...)
#define ESPAPPRTCM_DEBUG(...)
#else
#define ESPAPPRTCM_DEBUGDO(...) __VA_ARGS__
#define ESPAPPRTCM_DEBUG(...) ESPAPPRTCM_LOG(__VA_ARGS__)
#endif

#if ESPAPPRTCM_DEBUG_LEVEL < 2
#define ESPAPPRTCM_DEBUGVDO(...)
#define ESPAPPRTCM_DEBUGV(...)
#else
#define ESPAPPRTCM_DEBUGVDO(...) __VA_ARGS__
#define ESPAPPRTCM_DEBUGV(...) ESPAPPRTCM_LOG(__VA_ARGS__)
#endif

#if ESPAPPRTCM_DEBUG_LEVEL < 3
#define ESPAPPRTCM_DEBUGVVDO(...)
#define ESPAPPRTCM_DEBUGVV(...)
#else
#define ESPAPPRTCM_DEBUGVVDO(...) __VA_ARGS__
#define ESPAPPRTCM_DEBUGVV(...) ESPAPPRTCM_LOG(__VA_ARGS__)
#endif

#define RTCMEMORY_MAXLEN     512

#define RTCMEMORY_FULLBUFFER 1
//#define RTCMEMORY_FULLUPDATE 1

#ifndef RTCMEMORY_FULLBUFFER
#define RTCMEMORY_FULLBUFFER 0
#endif

#ifndef RTCMEMORY_FULLUPDATE
#define RTCMEMORY_FULLUPDATE 0
#endif

#define RTCMEMORY_TOTALSLOTS  (RTCMEMORY_MAXLEN/4)
#define RTCMEMORY_ARDUINORSV  (sizeof(struct eboot_command)/4)
#define RTCMEMORY_ACCESSSLOTS (RTCMEMORY_TOTALSLOTS-RTCMEMORY_ARDUINORSV)
#define RTCMEMORY_USERSLOTS   (RTCMEMORY_ACCESSSLOTS-(MD5_BINLEN/4))
#define RTCMEMORY_USERSTART   (RTCMEMORY_TOTALSLOTS-RTCMEMORY_USERSLOTS)

class RTCMemory {
	protected:
		struct RTCDATA {
			uint8_t sig[MD5_BINLEN];
			uint32_t data[RTCMEMORY_USERSLOTS];
		}
#if RTCMEMORY_FULLBUFFER
		_rtcData
#endif
		;

		RTCMemory();

	public:
		static RTCMemory &Manager();

		bool Read(uint8_t offset, uint32_t *buf, uint8_t count);
		bool Write(uint8_t offset, uint32_t const *buf, uint8_t count);
};

#endif //__RTCMEMORY_H__
