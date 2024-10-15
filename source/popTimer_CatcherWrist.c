
#include "popTimer_CatcherWrist.h"

#include "app_scheduler.h"
#include "commands.h"
#include "gpio.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "utility.h"
#include "system.h"
#include "ble_gap.h"


// Public Variables


//32.77 ticks per ms
//#define THROW_DURATION_MIN		APP_TIMER_TICKS(50)     // 50ms = 1638 ticks
#define THROW_DURATION_MIN		APP_TIMER_TICKS(80)     // 80ms = 2621 ticks
#define THROW_DURATION_MAX		APP_TIMER_TICKS(300)	// 300ms = 9831 ticks
#define THROW_DURATION_WDT		APP_TIMER_TICKS(300)	// 300ms = 9831 ticks

#define DURATION_10ms                   APP_TIMER_TICKS(10)      // 10ms = 328 ticks
#define DURATION_30ms                   APP_TIMER_TICKS(30)      // 30ms = 983 ticks
#define DURATION_60ms                   APP_TIMER_TICKS(60)      // 60ms = 1966 ticks


static volatile SessionInfo_t sessionInfo = {
	.groupId = UNGROUPED_ID,
	.deviceType = Device_CatcherWrist,
	.sessionID = SESSION_ID_INVALID,
	.timestamp = 0,
	.ficrID = 0
};

// Private Function Declarations
static void monitorBallThrow(void);
static void findThrow(void);
static void findThrow_MOCK(void);

// Private Function Definitions
static void monitorBallThrow(void) {
    // Monitors accelerometer patterns to determine if ball is thrown on axis Y and Z.

    findThrow();
    // findThrow_MOCK();
}

static void findThrow(void) {
	// Revised Throw Trigger June. 2023 modeled after utility app state machine 004
	static uint32_t lastSessionTriggered = 0;
	static ThrowTracking_t throwTrack = {.state = 0};

	// Allow one trigger per session.
	if (lastSessionTriggered == sessionInfo.sessionID) {
		return;
	}

    XYZ_t xyz = getXYZ();
	switch (throwTrack.state) {
		case 0:	// waiting for Z axis trigger T0
			//nrf_gpio_cfg_output(PIN_LED_GREEN);
			//pinClear(PIN_LED_GREEN);
			if (xyz.z <= -25000) {
				throwTrack.timeT0 = getNow();
				throwTrack.state = 1;
			}
			break;

		case 1:	// while Z is accelerating...
			if (xyz.z <= -25000) {   //maintain Y acceleration
				Timestamp_t now = getNow();
				int64_t duration = (now.ticks - throwTrack.timeT0.ticks);
				if (duration >= DURATION_30ms) {
					throwTrack.state = 2;
				}
			} else {  //Exit for false trigger (if X or Y > -20000 then exit)
				throwTrack.state = 0;
			}
			break;

		case 2: //find Y and Z
			if (xyz.y <= -25000 && xyz.z <= -25000) {
				throwTrack.timeT1 = getNow();
				throwTrack.state = 3;
			}
			break;

		case 3:
			if (xyz.y <= -25000 && xyz.z <= -25000) {
				Timestamp_t now = getNow();
				int64_t duration = (now.ticks - throwTrack.timeT1.ticks);
				if (duration >= DURATION_10ms) {
					if (xyz.x >= 0) {
						throwTrack.state = 4; //Trigger will be falling X axis
					} else {
						throwTrack.state = 5; //Trigger will be rising Z axis
					}
				}
			} else {
				throwTrack.state = 0;
			}
			break;

		case 4: //Find falling X axis trigger
			if (xyz.x <= -20000) {
				throwTrack.timeT2 = getNow();
				//pinSet(PIN_LED_GREEN);
				sessionInfo.timestamp.ticks = (int64_t) throwTrack.timeT2.ticks + DURATION_10ms - getTimestampOffset();
				lastSessionTriggered = sessionInfo.sessionID;
				startFastAdvertisingInterval();
				stopPopTimer();

				throwTrack.state = 0;
				debugPrint("Ball Thrown - X");
			}
			break;

		case 5: //Find rising Z axis trigger
			if (xyz.z >= -25000){
				throwTrack.timeT2 = getNow();
				//pinSet(PIN_LED_GREEN);
				sessionInfo.timestamp.ticks = (int64_t) throwTrack.timeT2.ticks - getTimestampOffset();
				lastSessionTriggered = sessionInfo.sessionID;
				startFastAdvertisingInterval();
				stopPopTimer();

				throwTrack.state = 0;
				debugPrint("Ball Thrown - Z");
			}
			break;

		default:
			throwTrack.state = 0;
			break;
	}

	if (throwTrack.state != 0) {
		Timestamp_t now = getNow();
		int64_t duration = (now.ticks - throwTrack.timeT0.ticks);
		if (duration > THROW_DURATION_WDT) {
			throwTrack.state = 0;
		}
	}
}

static void findThrow_MOCK(void) {
	// Mock throw
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

	if (diff >= 19500) {
		Timestamp_t now = getNow();
		sessionInfo.timestamp.ticks = (int64_t) now.ticks - getTimestampOffset();
		lastSessionTriggered = sessionInfo.sessionID;

		startFastAdvertisingInterval();
		stopPopTimer();
		debugPrint("Throw Done");

		// Reset
		lastAccel = 0;
	}
}


// Public Function Definitions
void popTimerLoop_CatcherWrist(void) {
	monitorBallThrow();
}

SessionInfo_t getSessionInfo_CatcherWrist(void) {
	// WARNING: CALLED IN INTERRUPT CONTEXT!!
	return sessionInfo;
}

uint32_t getSessionID_CatcherWrist(void) {
	return sessionInfo.sessionID;
}

void setSessionID_CatcherWrist(uint32_t id) {
	sessionInfo.sessionID = id;
}
