
#ifndef POP_TIMER_CATCHER_GLOVE_H
#define POP_TIMER_CATCHER_GLOVE_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>
#include "popTimer.h"

typedef PACKED_STRUCT {
	uint16_t groupId;
	uint32_t sessionID;
	Timestamp_t ballCatch1stTimestamp;
	float meters;
} PopTimer1stBallCatch_t;

typedef PACKED_STRUCT {
	uint16_t groupId;
	uint32_t sessionID;
	Timestamp_t ballThrowTimestamp;
} PopTimerBallThrow_t;

typedef enum {
	State_WaitFor1stBallCatch = 0,
	State_WaitForBallThrow = 1,
	State_WaitFor2ndBallCatch = 2,
	State_Complete = 3
} SessionState_t;


// Public Variables


// Public Function Declarations
void popTimerLoop_CatcherGlove(void);
void startPopTimer_CatcherGlove(uint32_t sessionID);
void stopPopTimer_CatcherGlove(void);
void wristAndFielderReady_CatcherGlove(uint32_t sessionID);
SessionInfo_t getSessionInfo_CatcherGlove(void);
ScanRespType_t getNextScanType_CatcherGlove(void);
void processSessionInfo_CatcherGlove(MfgData_Session_t *session);
PopTimerStats_t getStats(void);
bool getIsRunning_CatcherGlove(void);

#endif
