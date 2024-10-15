// system.h

#ifndef SYSTEM_H__
#define SYSTEM_H__

#include <stdint.h>
#include <nrfx.h>

typedef PACKED_STRUCT {
	int64_t ticks: 40;		// (5 bytes when PACKED) With prescale 0, 30.517 usec per tick @ 40-bit = overflow every 388 days
} Timestamp_t;

typedef enum {
	Sleep_WakeAll = 0,
	Sleep_ExtPeripherals = 1,
	Sleep_All = 2
} SleepSelect_t;

void logInit(void);
void powerManage(void);
void timerModuleInit(void);
void rtcInit(void);
Timestamp_t getNow(void);
void wakeInit(void);
void systemSleep(SleepSelect_t select);
bool isDwSleeping(void);
void enableCodeProtect(void);

#endif //SYSTEM_H__
