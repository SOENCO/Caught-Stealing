
#include "popTimer.h"

#include "app_scheduler.h"
#include "bleInitialize.h"
#include "centralHandler.h"
#include "commands.h"
#include "flashHandler.h"
#include "nrf_crypto.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "popTimer_CatcherGlove.h"
#include "popTimer_CatcherWrist.h"
#include "popTimer_FielderGlove.h"
#include "rxMain.h"
#include "txMain.h"
#include "stdlib.h"
#include "utility.h"
#include "gpio.h"

/*
	Devices:
		- Catcher Wrist sensor: nRF, accelerometer, no DW.
		- Catcher Glove sensor: nRF, accelerometer and DW.
			* Performs DW RX Reply role
			* Communicates with App during Pop Timer event.
		- Fielder Glove sensor: nRF, accelerometer and DW.
			* Performs DW TX Poll role

	BLE advertising:
		- Catcher's Glove sensor always advertises via BLE, even during connection with mobile app the following info:
			* Group ID (allows for grouping the 3 devices, ie allows multiple sets in same area)
			* Session ID
			* 64-bit tick system timestamp. Used as master clock and sync'd to other devices.
		- Catcher's Wrist sensor always advertises via BLE:
			* Group ID (allows for grouping the 3 devices, ie allows multiple sets in same area)
			* Session ID
			* 64-bit tick timestamp of Last Ball Throw event timestamp for the Session ID.
		- Fielder's Glove sensor always advertises via BLE:
			* Group ID
			* Session ID
			* 64-bit tick timestamp of Last Ball Catch event timestamp for the Session ID.

	Roles:
		- Catcher's Glove sensor:
			* BLE Peripheral: connects with mobile app
				- To receive device type: Catcher Glove
				- To receive start cmd.
				- To send PopTimer stats.
			* BLE Central: listens to Catcher's Wrist and Fielder's Glove advertisements for event timestamps for a given Session ID.
			* DecaWave: (RXer: sends Reply msg and calculates the distance between Catcher and Fielder)

		- Catcher's Wrist sensor:
			* BLE Peripheral: can connects with mobile app
				- To receive device type: Catcher Wrist. This not currently used since the lack of DW chip causes device to default to Wrist and prevents a device type change.
				- Can be used to stream accelerometer data.
				- Can be used to change Group ID
				- Should not be connected to mobile app during active PopTimer session.
			* BLE Central: listens to the Catcher's Glove timestamp advertisement to set its clock offset.

		- Fielder's Glove sensor:
			* BLE Peripheral: connects with mobile app
				- To receive device type change: Fielder Glove
				- Can be used to stream accelerometer data.
				- Can be used to change Group ID
				- Should not be connected to mobile app during active PopTimer session.
			* BLE Central: listens to the Catcher's Glove timestamp advertisement to set its clock offset.
			* DecaWave: (TXer: sends Poll & Final msgs)

	Sequence:
		1) App sends start cmd to Catcher's Glove via BLE.
		2) Catcher's Glove advertises an incremented Session ID.
		2) Catcher's Glove:
			a) Watches for Ball Catch event and logs event timestamp.
			b) Grabs latest distance-to-fielder value.
		3) Catcher's Wrist monitors Ball Throw event, logs event timestamp and advertises the event timestamp with the new Session ID (seen from Catcher's Glove).
			* Note: Even though Wrist seens a new Session ID in Catcher's Glove adverts, it does not change its Session ID (in advert packet) until a new Ball Throw event occurs.
		4) Fielder's Glove watches for Ball Catch event, logs event timestamp and advertises the event timestamp with the new Session ID. (seen from Catcher's Glove).
			* Note: Even though Fielder seens a new Session ID in Catcher's Glove adverts, it does not change its Session ID (in advert packet) until a new Ball Catch event occurs.
		5) Catcher's Glove:
			a) Waits for Catcher's Wrist & Fielder's Glove event timestamps that match the new Session ID.
			b) Sends PopTimer stats to mobile app.
*/


APP_TIMER_DEF(sessionTimeout_timerId);
APP_TIMER_DEF(fastAdvertInterval_timerId);

typedef PACKED_STRUCT {
	uint32_t sessionID;
	Timestamp_t offset;
} SessionData_t;

// Public Variables
uint64_t ficrID = 0;
DeviceType_t device = Device_Unknown;
char appSleepCmd[APP_SLEEP_CMD_MAX_PLUS_NULL] = "PopSlp000"; // Advertised Name: Using for Scan filtering

// Private Variables
volatile static bool isNonCatcherGloveRunning = false;
volatile static int64_t timestampOffset = 0;	// Used by Catcher Wrist & Fielder Glove ONLY.


// Private Function Declarations
static void configDeviceByType(DeviceType_t type);
static void processSessionInfo(MfgData_Session_t *session);
static void startPopTimer_AppContext(void *p_event_data, uint16_t event_size);
static void stopPoptimer_AppContext(void *p_event_data, uint16_t event_size);
static void setSessionID(uint32_t id);
static uint32_t getSessionID(void);
static ScanRespType_t getNextScanType_NonCatcherGlove(void);
static bool isValidName(uint8_t *name);
static void receivedMobileAppCmd(ble_gap_evt_adv_report_t const *p_adv_report);

static void timersInit(void);
static void sessionTimeout_handler(void *context);
static void fastAdvertIntervalTimeout_start(void);
static void fastAdvertIntervalTimeout_stop(void);
static void fastAdvertIntervalTimeout_handler(void *context);
static void fastAdvertIntervalTimeout_handler_AppContext(void* p_event_data, uint16_t event_size);


// Private Function Definitions
static void configDeviceByType(DeviceType_t type) {
	NRF_LOG_INFO("Configure Device Type: %d", type);

	switch (type) {
		case Device_CatcherGlove:
			txDeInit();
			rxInit();
			break;
		case Device_CatcherWrist:
			txDeInit();
			rxDeInit();
			break;
		case Device_FielderGlove:
			rxDeInit();
			txInit();
			break;
		default:
			NRF_LOG_ERROR("Config ERROR: unknown device value");
			break;
	}

	setGapName();
	setDeviceAppearance();
	setScanFilter();
	advertisingStart(false); // Ensure always advertising
	centralStartScan();	// Ensure always scanning
	printBanner();
}

static void processSessionInfo(MfgData_Session_t *session) {
	// WARNING: CALLED IN INTERRUPT CONTEXT!!
	// NOTE: BE CAREFUL NOT TO DELAY GETTING CATCHER SESSION IDS AND MISS EVENTS.
	ret_code_t err_code;

	// See if from Catcher's Glove and known
	SessionInfo_t *info = &session->info;
	if ((info->deviceType != Device_CatcherGlove) || (info->ficrID != storage.catcherGloveFICR)) {
		NRF_LOG_INFO("Invalid Catcher Glove");
		return;
	}

	// If Catcher's Glove not actively running a Pop session, ignore packet.
	if ((!session->flags.isRunning) || (info->sessionID == SESSION_ID_INVALID)) {
		// See if this device is running. If yes, stop.
		if (isNonCatcherGloveRunning) {
			err_code = app_sched_event_put(NULL, 0, stopPoptimer_AppContext);
			APP_ERROR_CONTINUE(err_code);
		}
		// NRF_LOG_INFO("!isRunning || SESSION_ID_INVALID");
		return;
	}

	// See if session id changed.
	if (getSessionID() != info->sessionID) {
		setSessionID(info->sessionID);
		startPopTimer_AppContext(NULL, 0);
	}

	// Calculate Catcher Glove timestamp sync offset
	Timestamp_t now = getNow();
	timestampOffset = now.ticks - info->timestamp.ticks;
}

static void startPopTimer_AppContext(void *p_event_data, uint16_t event_size) {
	// Used by Catcher Wrist & Fielder Glove ONLY.
	startPopTimer_NonCatcherGlove();
}

static void stopPoptimer_AppContext(void *p_event_data, uint16_t event_size) {
	stopPopTimer();
}

static void setSessionID(uint32_t id) {
	switch (device) {
		case Device_CatcherGlove:
			break;
		break;

		case Device_CatcherWrist:
			setSessionID_CatcherWrist(id);
		break;

		case Device_FielderGlove:
			setSessionID_FielderGlove(id);
		break;

		default:
		break;
	}
}

static uint32_t getSessionID(void) {
	switch (device) {
		case Device_CatcherGlove:
			return SESSION_ID_INVALID;
		break;

		case Device_CatcherWrist:
			return getSessionID_CatcherWrist();
		break;

		case Device_FielderGlove:
			return getSessionID_FielderGlove();
		break;

		default:
			return SESSION_ID_INVALID;
	}
}

static ScanRespType_t getNextScanType_NonCatcherGlove(void) {
	// Toggles the scan response type to broadcast.
	// For catcher wrist or fielder glove: DO NOT USE FOR CATCHER GLOVE.
	// - Toggles between ScanRespType_Name & ScanRespType_Session & ScanRespType_Info.
	// - Never adverts ScanRespType_Stats.

	static ScanRespType_t nextType = ScanRespType_Name;

	if (isNonCatcherGloveRunning) {
		return ScanRespType_Session;
	}

	switch (nextType) {
		case ScanRespType_Name:
			nextType = ScanRespType_Session;
		break;

		case ScanRespType_Session:
			nextType = ScanRespType_Info;
		break;

		case ScanRespType_Info:
			nextType = ScanRespType_Name;
		break;

		case ScanRespType_Stats:
			// Should not hit here.
		break;
	}
	return nextType;
}

static bool isValidName(uint8_t *name) {
	// Checks if empty (first char '\0') or invalid character in first position. I.e. need at least one printable character for Centrals to see advertisment.
	return (name[0] > 32) && (name[0] < 127);
}

static void receivedMobileAppCmd(ble_gap_evt_adv_report_t const *p_adv_report) {
	// Checks advert packet for mobile app cmds

	// Check for sleep cmd
	if (isSubset(p_adv_report->data.p_data, appSleepCmd, p_adv_report->data.len, APP_SLEEP_CMD_MAX)) {
		systemSleep(Sleep_All);
	}
}

static void timersInit(void) {
	// Init timers
	uint32_t err_code = app_timer_create(&sessionTimeout_timerId, APP_TIMER_MODE_SINGLE_SHOT, sessionTimeout_handler);
	APP_ERROR_CONTINUE(err_code);

	err_code = app_timer_create(&fastAdvertInterval_timerId, APP_TIMER_MODE_SINGLE_SHOT, fastAdvertIntervalTimeout_handler);
	APP_ERROR_CONTINUE(err_code);
}

static void sessionTimeout_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, stopPoptimer_AppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void fastAdvertIntervalTimeout_start(void) {
	app_timer_stop(fastAdvertInterval_timerId);
	uint32_t err_code = app_timer_start(fastAdvertInterval_timerId, APP_TIMER_TICKS(3000), NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void fastAdvertIntervalTimeout_stop(void) {
	app_timer_stop(fastAdvertInterval_timerId);
}

static void fastAdvertIntervalTimeout_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, fastAdvertIntervalTimeout_handler_AppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void fastAdvertIntervalTimeout_handler_AppContext(void* p_event_data, uint16_t event_size) {
	// In App Context

	// Restart advertising with slower interval.
	advertisingStart(false);
	// Restart central scan which was turned off after event trigger
	centralStartScan();
}


// Public Function Definitions
void popTimerInit(void) {
    ret_code_t err_code = nrf_crypto_init();
    APP_ERROR_CHECK(err_code);

	timersInit();
	configDeviceByType(device);
}

void popTimerLoop(void) {
	switch (device) {
		case Device_CatcherGlove:
			popTimerLoop_CatcherGlove();
			break;
		case Device_CatcherWrist:
			popTimerLoop_CatcherWrist();
			break;
		case Device_FielderGlove:
			popTimerLoop_FielderGlove();
			break;
		default:
			break;
	}
}

void startPopTimer_NonCatcherGlove(void) {
	// Initializes session and starts pop timer.
	debugPrint("PopTimer: Start");

	systemSleep(Sleep_WakeAll);
	isNonCatcherGloveRunning = true;
}

void stopPopTimer(void) {
	debugPrint("PopTimer: Stop");

	sessionTimeout_stop();
	systemSleep(Sleep_ExtPeripherals);
	isNonCatcherGloveRunning = false;

	if (device == Device_CatcherGlove) {
		stopPopTimer_CatcherGlove();
	}
}

void setName(uint8_t *name, uint8_t size) {
	// Cannot have a zero length name or centrals will not see it advertise.
	if (!isValidName(name)) {
		debugPrint("set Name: Invalid name, ignored. First char: %d", name[0]);
		return;
	}

	uint8_t safeLength = (size <= DEVICE_NAME_LENGTH) ? size : DEVICE_NAME_LENGTH;
	memset(storage.fullname, 0, sizeof(storage.fullname));
	memcpy(storage.fullname, name, safeLength);

	updateFlash();
	setGapName();
	sendName();
}

void setSessionTimeout(uint16_t timeout) {
	if ((timeout < SESSION_TIMEOUT_MIN_SECONDS) || (timeout > SESSION_TIMEOUT_MAX_SECONDS)) {
		debugPrint("set SessionTimeout: Invalid value: %d", timeout);
		return;
	} else if (storage.sessionTimeout == timeout) {
		debugPrint("set SessionTimeout: No change same value: %d", timeout);
		return;
	}

	storage.sessionTimeout = timeout;

	updateFlash();
	sendSessionTimeout();
}

void setOwner(uint32_t ownerId) {
	debugPrint("Set Owner: %d", ownerId);

	storage.ownerId = ownerId;
	updateFlash();
	sendDeviceInfo();
}

SessionInfo_t getSessionInfo(void) {
	SessionInfo_t sessionInfo = {0};
	switch (device) {
		case Device_CatcherGlove:
			sessionInfo = getSessionInfo_CatcherGlove();
		break;

		case Device_CatcherWrist:
			sessionInfo = getSessionInfo_CatcherWrist();
		break;

		case Device_FielderGlove:
			sessionInfo = getSessionInfo_FielderGlove();
		break;

		default:
		break;
	}

	// Ensure group is up to date.
	sessionInfo.groupId = storage.groupId;
	sessionInfo.ficrID = ficrID;
	return sessionInfo;
}

ScanRespType_t getNextScanType(void) {
	ScanRespType_t type = ScanRespType_Name;
	switch (device) {
		case Device_CatcherGlove:
			type = getNextScanType_CatcherGlove();
		break;

		case Device_CatcherWrist:
		case Device_FielderGlove:
			type = getNextScanType_NonCatcherGlove();
		break;

		default:
		break;
	}
	return type;
}

bool getIsRunning(void) {
	bool running = false;
	switch (device) {
		case Device_CatcherGlove:
			running = getIsRunning_CatcherGlove();
		break;

		case Device_CatcherWrist:
		case Device_FielderGlove:
			running = isNonCatcherGloveRunning;
		break;

		default:
		break;
	}
	return running;
}

void receivedBleScanResponse(nrf_ble_scan_evt_filter_match_t filterMatchEvent) {
	// WARNING: CALLED IN INTERRUPT CONTEXT!!
	ble_gap_evt_adv_report_t const *p_adv_report = filterMatchEvent.p_adv_report;
	nrf_ble_scan_filter_match filter_match = filterMatchEvent.filter_match;

	// See if mobile app advertisement packet.
	if (filter_match.name_filter_match) {
		receivedMobileAppCmd(p_adv_report);
		return;
	}

	// Scan responses only
	if (!p_adv_report->type.scan_response) {
		return;
	}

	// Check length
	if (p_adv_report->data.len < sizeof(ScanResponse_Session_t)) {
		return;
	}

	// See if Session Info advertisement packet, which is sent with appearance attribute.
	if (!filter_match.appearance_filter_match) {
		return;
	}

	// Convert to advert struct
	ScanResponse_Session_t *advert = (ScanResponse_Session_t*) p_adv_report->data.p_data;

	// Ensure session packet type
	if (advert->mfgData.type != MfgDataType_Session) {
		return;
	}

	MfgData_Session_t *session = &advert->mfgData;
	switch (device) {
		case Device_CatcherGlove:
			processSessionInfo_CatcherGlove(session);
			break;
		case Device_CatcherWrist:
		case Device_FielderGlove:
			processSessionInfo(session);
			break;
		default:
			break;
	}
}

char* getDeviceName(void) {

	// See if custom name in Flash
	bool isValid = isValidName(storage.fullname);
	bool isCustom = (strncmp(storage.fullname, NO_CUSTOM_NAME, DEVICE_NAME_LENGTH) != 0);

	if (isValid && isCustom) {
		return storage.fullname;
	} else {
		// No custom name, use defaults.
		switch (device) {
			case Device_CatcherGlove:
				return CATCHER_GLOVE;
			case Device_CatcherWrist:
				return CATCHER_WRIST;
			case Device_FielderGlove:
				return FIELDER_GLOVE;
			default:
				return "NAME ERROR";
		}
	}
}

void hardwareError(void) {
	nrf_gpio_cfg_output(PIN_LED_GREEN);
	pinSet(PIN_LED_GREEN);
}

void startFastAdvertisingInterval(void) {
	// Use for Wrist and Fielder Glove to increase advert broadcasting of session events, temporarily.
	fastAdvertIntervalTimeout_start();
	centralStopScan();
	advertisingStart(true);
}

int64_t getTimestampOffset(void) {
	// Ensure atomic read.
	int64_t offset = 0;

	do {
		offset = timestampOffset;
	} while (offset != timestampOffset);

	return offset;
}

void sessionTimeout_start(uint16_t seconds) {
	app_timer_stop(sessionTimeout_timerId);
	uint32_t err_code = app_timer_start(sessionTimeout_timerId, APP_TIMER_TICKS(seconds * 1000), NULL);
	APP_ERROR_CONTINUE(err_code);
}

void sessionTimeout_stop(void) {
	app_timer_stop(sessionTimeout_timerId);
}
