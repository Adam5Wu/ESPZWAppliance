#ifndef __ESPZWAppliance_H__
#define __ESPZWAppliance_H__

#include <sys/time.h>

extern void WebPortal_TimedStart();
extern void WebPortal_Stop();
extern void Device_Restart();

#define setup __userapp_setup
#define teardown __userapp_teardown
#define loop __userapp_loop

#endif //__ESPZWAppliance_H__