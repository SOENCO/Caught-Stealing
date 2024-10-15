
#include "popTimer_FielderGlove.h"

#include "app_scheduler.h"
#include "commands.h"
#include "flashHandler.h"
#include "gpio.h"
#include "LIS2DW12.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "stdlib.h"
#include "utility.h"

// Public Variables


// Private Variables
#define FIELDER_CATCH_THRESHOLD	19500
static volatile SessionInfo_t sessionInfo = {
	.groupId = UNGROUPED_ID,
	.deviceType = Device_FielderGlove,
	.sessionID = SESSION_ID_INVALID,
	.timestamp = 0,
	.ficrID = 0
};


// Private Function Declarations
static void monitorBallCatch(void);

// Private Function Definitions
static void monitorBallCatch(void) {
	// Monitors accelerometer patterns to determine if ball is thrown.
	static int32_t lastAccel = 0;
	static uint32_t lastSessionTriggered = 0;

	// Allow one trigger per session.
	if (lastSessionTriggered == sessionInfo.sessionID) {
		return;
	}

	XYZ_t xyz = getXYZ();
	int32_t accelNow = abs(xyz.x) + abs(xyz.y) + abs(xyz.z);
	int32_t diff = abs(accelNow - lastAccel);
	lastAccel = accelNow;

	if (diff >= FIELDER_CATCH_THRESHOLD) {
		debugPrint("ball catch (fielder)");
		Timestamp_t now = getNow();
		sessionInfo.timestamp.ticks = (int64_t) now.ticks - getTimestampOffset();
		lastSessionTriggered = sessionInfo.sessionID;

		startFastAdvertisingInterval();
		stopPopTimer();

		// Reset
		lastAccel = 0;
	}
}


// Public Function Definitions
void popTimerLoop_FielderGlove(void) {
	monitorBallCatch();
}

SessionInfo_t getSessionInfo_FielderGlove(void) {
	// WARNING: CALLED IN INTERRUPT CONTEXT!!
	return sessionInfo;
}

uint32_t getSessionID_FielderGlove(void) {
	return sessionInfo.sessionID;
}

void setSessionID_FielderGlove(uint32_t id) {
	sessionInfo.sessionID = id;
}
