
#ifndef POP_TIMER_H
#define POP_TIMER_H

#include <stdint.h>
#include <stdbool.h>
#include "ble_srv_common.h"
#include "bleInitialize.h"
#include "nrf_ble_scan.h"
#include "system.h"
#include <nrfx.h>

// Keep names <= 20 or scan response advert packet will trim and change to SHORTEN NAME instead of COMPLETE NAME.
#define DEVICE_NAME_LENGTH				20	// WARNING: CHANGING THIS AFFECTS FLASH STRUCT SIZE!!!!
#define DEVICE_NAME_LENGTH_PLUS_NULL	(DEVICE_NAME_LENGTH + 1)
#define CATCHER_GLOVE		"CATCH GLVE"
#define CATCHER_WRIST		"CATCH WRST"
#define FIELDER_GLOVE		"FIELD GLVE"
#define NO_CUSTOM_NAME		"NOT CUSTOM"
#define UNOWNED_ID							0
#define UNGROUPED_ID						0
#define DEFAULT_SESSION_TIMEOUT_SECONDS		30
#define SESSION_TIMEOUT_MIN_SECONDS			5
#define SESSION_TIMEOUT_MAX_SECONDS			(60 * 5)

#define SESSION_ID_INVALID	0
#define TIMESTAMP_INVALID	0

typedef enum {
	Device_Unknown = 0,
	Device_CatcherGlove = 1,
	Device_CatcherWrist = 2,
	Device_FielderGlove = 3
} DeviceType_t;

typedef enum {
	MfgDataType_Session = 0,
	MfgDataType_Stat = 1,
	MfgDataType_Version = 2,
	MfgDataType_Info = 3
} MfgDataType_t;

typedef enum {
	ScanRespType_Name = 0,
	ScanRespType_Session = 1,
	ScanRespType_Stats = 2,
	ScanRespType_Info = 3
} ScanRespType_t;

typedef PACKED_STRUCT {
	bool isShaken: 1;
	bool isAuthed: 1;
	bool isRunning: 1;
	uint8_t empty: 5;
} MfgDataFlags_t;

// ******************* Session Info *******************

typedef PACKED_STRUCT {
	uint16_t groupId;
	DeviceType_t deviceType;
	uint32_t sessionID;
	Timestamp_t timestamp;		// Different based on device type (time sync OR event time).
	uint64_t ficrID;
} SessionInfo_t;				// 20 bytes

typedef PACKED_STRUCT {
	MfgDataType_t type;
	uint8_t battery;
	MfgDataFlags_t flags;
	SessionInfo_t info;
} MfgData_Session_t;			// 23 bytes

typedef PACKED_STRUCT {
	uint8_t appearanceLength;
	uint8_t appearanceFlag;
	uint16_t appearance;
	uint8_t mfgLength;
	uint8_t mfgFlag;
	uint8_t companyId[2];		// 8 bytes to this point
	MfgData_Session_t mfgData;
} ScanResponse_Session_t;		// 31 bytes

// ******************* Stats Info *******************

typedef PACKED_STRUCT {
	uint16_t groupId;
	uint32_t sessionID;
	Timestamp_t ballCatch1stTimestamp;
	Timestamp_t ballThrowTimestamp;
	Timestamp_t ballCatch2ndTimestamp;
	float meters;
} PopTimerStats_t;				// 25 bytes

typedef PACKED_STRUCT {
	MfgDataType_t type;
	PopTimerStats_t stats;
} MfgData_Stats_t;				// 26 bytes

typedef PACKED_STRUCT {
	uint8_t mfgLength;
	uint8_t mfgFlag;
	uint8_t companyId[2];		// 4 bytes to this point
	MfgData_Stats_t mfgData;
} ScanResponse_Stats_t;			// 30 bytes

// ******************* Name *******************

typedef PACKED_STRUCT {
	MfgDataType_t type;
	uint32_t version: 24;		// ex: 100 integer (1.00)
} MfgData_Version_t;			// 4 bytes

typedef PACKED_STRUCT {
	uint8_t fullNameLength;
	uint8_t fullNameFlag;
	uint8_t name[DEVICE_NAME_LENGTH];
	uint8_t mfgLength;
	uint8_t mfgFlag;
	uint8_t companyId[2];
	MfgData_Version_t mfgData;
} ScanResponse_Name_t;	// 31 bytes

typedef PACKED_STRUCT {
	MfgDataType_t type;
	int8_t temperatureCelsius;
	uint32_t ownerId;
} MfgData_Info_t;			// 6 bytes

typedef PACKED_STRUCT {
	uint8_t mfgLength;
	uint8_t mfgFlag;
	uint8_t companyId[2];
	MfgData_Info_t mfgData;
} ScanResponse_Info_t;	// 10 bytes

#define APP_SLEEP_CMD_MAX			11
#define APP_SLEEP_CMD_MAX_PLUS_NULL	(APP_SLEEP_CMD_MAX + 1)


// Public Variables
extern uint64_t ficrID;
extern DeviceType_t device;
extern char appSleepCmd[APP_SLEEP_CMD_MAX_PLUS_NULL];


// Public Function Declarations
void popTimerInit(void);
void popTimerLoop(void);
void startPopTimer_NonCatcherGlove(void);
void stopPopTimer(void);
void setName(uint8_t *name, uint8_t size);
void setSessionTimeout(uint16_t timeout);
void setOwner(uint32_t ownerId);
SessionInfo_t getSessionInfo(void);
ScanRespType_t getNextScanType(void);
bool getIsRunning(void);
void receivedBleScanResponse(nrf_ble_scan_evt_filter_match_t filterMatchEvent);
char* getDeviceName(void);
void hardwareError(void);
void startFastAdvertisingInterval(void);
int64_t getTimestampOffset(void);
void sessionTimeout_start(uint16_t seconds);;
void sessionTimeout_stop(void);

#endif
