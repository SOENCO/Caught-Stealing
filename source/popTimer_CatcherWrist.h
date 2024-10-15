
#ifndef POP_TIMER_CATCHER_WRIST_H
#define POP_TIMER_CATCHER_WRIST_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>
#include "popTimer.h"
#include "LIS2DW12.h"


typedef struct {
	int posZT0:1;
	int negYT1:1;
	int negZT2:1;
	int negYT3:1;
} trackState_t;

//Revised useage to track active throw sequence
typedef struct {
	int16_t valueT0;
	Timestamp_t timeT0;
	int16_t valueT1;
	Timestamp_t timeT1;
	int16_t valueT2;
	Timestamp_t timeT2;
	uint8_t state;		// bit 0= +Z trig, bit 1= -Y trig,  bit 2= -Z trig
} ThrowTracking_t;


// Public Variables


// Public Function Declarations
void popTimerLoop_CatcherWrist(void);
SessionInfo_t getSessionInfo_CatcherWrist(void);
uint32_t getSessionID_CatcherWrist(void);
void setSessionID_CatcherWrist(uint32_t id);

#endif
