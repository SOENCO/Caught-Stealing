
#include "popTimer_CatcherGlove.h"

#include "app_scheduler.h"
#include "bleInitialize.h"
#include "centralHandler.h"
#include "commands.h"
#include "gpio.h"
#include "flashHandler.h"
#include "LIS2DW12.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "rxMain.h"
#include "stdlib.h"
#include "system.h"
#include "utility.h"


// Public Variables


// Private Variables
#define CATCHER_CATCH_THRESHOLD 30000
static uint32_t sessionIDToAdvertise = SESSION_ID_INVALID;
static bool isPopRunning = false;
static bool isWristAndFielderReady = false;
static volatile PopTimerStats_t stats = {0};
static SessionState_t mainState = State_WaitFor1stBallCatch;
static volatile SessionInfo_t sessionInfo = {
	.groupId = UNGROUPED_ID,
	.deviceType = Device_CatcherGlove,
	.sessionID = SESSION_ID_INVALID,
	.timestamp.ticks = 0,
	.ficrID = 0
};

// Private Function Declarations
static void clearStats(void);
static bool waitFor1stBallCatch(void);
static void receivedBallThrow(void *p_event_data, uint16_t event_size);
static void received2ndBallCatch(void *p_event_data, uint16_t event_size);

// Private Function Definitions
static void clearStats(void) {
	memset((uint8_t*)&stats, 0, sizeof(stats));
	stats.ballCatch1stTimestamp.ticks = TIMESTAMP_INVALID;
	stats.ballThrowTimestamp.ticks = TIMESTAMP_INVALID;
	stats.ballCatch2ndTimestamp.ticks = TIMESTAMP_INVALID;
}

static bool waitFor1stBallCatch(void) {
	// Monitors accelerometer patterns to determine if ball is caught.
	static int32_t lastAccel = 0;

	XYZ_t xyz = getXYZ();
	int32_t accelNow = abs(xyz.x) + abs(xyz.y) + abs(xyz.z);
	int32_t diff = abs(accelNow - lastAccel);
	lastAccel = accelNow;

	if (diff >= CATCHER_CATCH_THRESHOLD) {
		stats.ballCatch1stTimestamp = getNow();
		stats.meters = getLastDistance();
		debugPrint("POP: 1st Ball Catch");

		// Send partial session info so app can see Pop progress.
		PopTimer1stBallCatch_t packet = {0};
		packet.groupId = storage.groupId;
		packet.sessionID = stats.sessionID;
		packet.ballCatch1stTimestamp = stats.ballCatch1stTimestamp;
		packet.meters = stats.meters;
		sendDataResponsePacket(Cmd_1stBallCatch, (uint8_t*)&packet, sizeof(packet));

		// Reset
		lastAccel = 0;
		return true;
	}
	return false;
}

static void receivedBallThrow(void *p_event_data, uint16_t event_size) {
	SessionInfo_t *payload = (SessionInfo_t *)p_event_data;
	stats.ballThrowTimestamp = payload->timestamp;
	debugPrint("POP: Received Ball Throw");

	// Send partial session info so app can see Pop progress.
	PopTimerBallThrow_t packet = {0};
	packet.groupId = storage.groupId;
	packet.sessionID = stats.sessionID;
	packet.ballThrowTimestamp = stats.ballThrowTimestamp;
	sendDataResponsePacket(Cmd_BallThrow, (uint8_t*)&packet, sizeof(packet));
}

static void received2ndBallCatch(void *p_event_data, uint16_t event_size) {
	SessionInfo_t *payload = (SessionInfo_t *)p_event_data;
	stats.ballCatch2ndTimestamp = payload->timestamp;
	debugPrint("POP: Received 2nd Ball Catch");
}


// Public Function Definitions
void popTimerLoop_CatcherGlove(void) {
	if (!isPopRunning || !isWristAndFielderReady) {
		return;
	}

	switch (mainState) {
		case State_WaitFor1stBallCatch:
			if (waitFor1stBallCatch()) {
				mainState = State_WaitForBallThrow;
			};
			break;
		case State_WaitForBallThrow:
			// Wait til Catcher Wrist adverts that ball is thrown.
			if (stats.ballThrowTimestamp.ticks != TIMESTAMP_INVALID) {
				mainState = State_WaitFor2ndBallCatch;
			};
			break;
		case State_WaitFor2ndBallCatch:
			// Wait til Fielder Glove adverts that ball is caught.
			if (stats.ballCatch2ndTimestamp.ticks != TIMESTAMP_INVALID) {
				mainState = State_Complete;
				sendDataResponsePacket(Cmd_PopStats, (uint8_t*)&stats, sizeof(stats));
				stopPopTimer();
			};
			break;
		case State_Complete:
			break;
	}
}

void startPopTimer_CatcherGlove(uint32_t sessionID) {
	// Initializes session and starts pop timer.
	debugPrint("Catcher Glove PopTimer: Start Session: %lu", sessionID);

	systemSleep(Sleep_WakeAll);

	sessionIDToAdvertise = sessionID;

	// Init Stats
	clearStats();
	clearDistance();
	stats.groupId = storage.groupId;
	stats.sessionID = sessionIDToAdvertise;

	// Restart state machine.
	mainState = State_WaitFor1stBallCatch;
	isPopRunning = true;
	isWristAndFielderReady = false; 	// Init to false on a new session.

	noBleActivity_stop();		// Stop BLE activity timer while in active session.
	advertisingStart(false);	// Ensure always advertising
	centralStartScan();			// Ensure always scanning

	// Only use session timeout for Catcher Glove. Other devices don't receive the session timeout value during a start Poptimer cmd. Therefore let them catch the stop when Catcher's Glove advertises SESSION_ID_INVALID.
	sessionTimeout_start(storage.sessionTimeout);
}

void stopPopTimer_CatcherGlove(void) {
	isPopRunning = false;
	isWristAndFielderReady = false;
	sessionIDToAdvertise = SESSION_ID_INVALID;	// Allows watching wrist & fielder devices to sleeop their external peripherals.
	noBleActivity_start();
}

void wristAndFielderReady_CatcherGlove(uint32_t sessionID) {
	// Ensure session id matches.
	if (sessionIDToAdvertise == sessionID) {
		isWristAndFielderReady = true;
	}
}

SessionInfo_t getSessionInfo_CatcherGlove(void) {
	// WARNING: CALLED IN INTERRUPT CONTEXT!!
	sessionInfo.sessionID = sessionIDToAdvertise;
	sessionInfo.timestamp = getNow();
	return sessionInfo;
}

ScanRespType_t getNextScanType_CatcherGlove(void) {
	// Toggles the scan response type to broadcast.
	// If catcher glove:
	// * If active session:
	//		- Adverts ScanRespType_Session.
	//		- Skips ScanRespType_Name & ScanRespType_Stats.
	// * If no session: toggles thru all types.

	static ScanRespType_t nextType = ScanRespType_Name;

	if (isPopRunning) {
		return ScanRespType_Session;
	}

	// Not actively running a session.
	switch (nextType) {
		case ScanRespType_Name:
			nextType = ScanRespType_Session;
		break;

		case ScanRespType_Session:
			nextType = ScanRespType_Stats;
		break;

		case ScanRespType_Stats:
			nextType = ScanRespType_Info;
		break;

		case ScanRespType_Info:
			nextType = ScanRespType_Name;
		break;
	}

	return nextType;
}

void processSessionInfo_CatcherGlove(MfgData_Session_t *session) {
	// WARNING: CALLED IN INTERRUPT CONTEXT!!

	// Debug print limiting.
	static bool allowDebugWrist = true;
	static bool allowDebugFielder = true;

	// Only should be listening if active session.
	if (!isPopRunning) {
		return;
	}

	// Only interested in advertisements after catching 1st ball.
	if (mainState == State_WaitFor1stBallCatch) {
		return;
	}

	// Check Session ID and if isRunning == true, then event has not yet occurred for this session id for that device.
	SessionInfo_t *info = &session->info;
	if ((info->sessionID == SESSION_ID_INVALID) || (info->sessionID != sessionIDToAdvertise) || session->flags.isRunning) {
		return;
	}

	// Check that device FICR is known.
	if ((info->ficrID != storage.catcherWristFICR) && (info->ficrID != storage.fielderGloveFICR)) {
		return;
	}

	// Safety check: ensure timestamp is valid
	if (info->timestamp.ticks == TIMESTAMP_INVALID) {
		return;
	}

	// Check device type
	ret_code_t err_code;
	switch (info->deviceType) {
		case Device_CatcherWrist:
			// Only receive once per session. Order of receiving doesn't matter.
			if (stats.ballThrowTimestamp.ticks == TIMESTAMP_INVALID) {
				if (info->timestamp.ticks > stats.ballCatch1stTimestamp.ticks) {
					allowDebugWrist = true;
					err_code = app_sched_event_put(info, sizeof(SessionInfo_t), receivedBallThrow);
				} else if (allowDebugWrist) {
					allowDebugWrist = false;
					debugPrint("Ignored Invalid Wrist");
				}
			}
			break;
		case Device_FielderGlove:
			// Only receive once per session. Order of receiving doesn't matter.
			if (stats.ballCatch2ndTimestamp.ticks == TIMESTAMP_INVALID) {
				if (info->timestamp.ticks > stats.ballThrowTimestamp.ticks) {
					allowDebugFielder = true;
					err_code = app_sched_event_put(info, sizeof(SessionInfo_t), received2ndBallCatch);
				} else if (allowDebugFielder) {
					allowDebugFielder = false;
					debugPrint("Ignored Invalid Fielder");
				}
			}
			break;
		default:
			debugPrint("Wrong device type");
			break;
	}
}

PopTimerStats_t getStats(void) {
	return stats;
}

bool getIsRunning_CatcherGlove(void) {
	return isPopRunning;
}