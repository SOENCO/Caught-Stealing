
#ifndef COMMANDS_H
#define COMMANDS_H

#include "ble_gap.h"
#include "bleServices.h"
#include "dwCommon.h"
#include "passkey.h"
#include "popTimer.h"
#include <limits.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <nrfx.h>



typedef enum {
	Cmd_Unknown = 0,
	// No longer set by app: Cmd_DeviceType = 1,
	Cmd_Distance = 2,
	Cmd_Accelerometer = 3,
	Cmd_StartPopTimer = 4,
	Cmd_StopPopTimer = 5,
	Cmd_1stBallCatch = 6,
	Cmd_BallThrow = 7,
	// No 2nd Ball Catch Cmd since sent with stats.
	Cmd_PopStats = 8,
	Cmd_GroupConfig = 9,
	Cmd_GetDeviceInfo = 10,
	Cmd_DebugInfo = 11,
	Cmd_NameChange = 12,
	Cmd_SessionTimeout = 13,
	Cmd_UnGroup = 14,
	Cmd_Owner = 15,
	Cmd_WristFielderReady = 16,
	Cmd_AuthCentralRequest = 0xA1,
	Cmd_AuthPeripheralResponse = 0xA2,
	Cmd_AuthCentralResponse = 0xA3,
	Cmd_AuthPeripheralResult = 0xA4
} CmdType_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint8_t parameter[COMMAND_MAX_SIZE - 1];
} CommandPacket_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	DeviceType_t deviceType;
} CommandPacket_Mode_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	bool enable;
} CommandPacket_Stream_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint16_t sessionTimeout;
	uint32_t sessionID;
} CommandPacket_StartPopTimer_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint16_t groupId;
	uint64_t catcherGloveFICR;
	uint64_t catcherWristFICR;
	uint64_t fielderGloveFICR;
} CommandPacket_GroupConfig_t;

typedef PACKED_STRUCT {
	DeviceType_t deviceType;
	uint16_t groupId;
	bool isStreamingDistance;
	uint64_t catcherGloveFICR;
	uint64_t catcherWristFICR;
	uint64_t fielderGloveFICR;
	uint32_t ownerId;
} DeviceInfo_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint8_t name[DEVICE_NAME_LENGTH];
} CommandPacket_NameChange_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint16_t sessionTimeout;
} CommandPacket_SessionTimeout_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint32_t ownerId;
} CommandPacket_Owner_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint32_t ownerId;
} CommandPacket_Ungroup_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint32_t sessionID;
} CommandPacket_WristFielderReady_t;

typedef PACKED_STRUCT {
	keys_t keys;
	uint8_t passkey[PASSKEY_SIZE];
	uint8_t bleMac_Phone[BLE_GAP_ADDR_LEN];
	uint8_t bleMac_Device[BLE_GAP_ADDR_LEN];
} Auth01_t;

typedef PACKED_STRUCT {
	uint8_t bleMac[BLE_GAP_ADDR_LEN];
	keys_t keys;
	uint8_t passkey[PASSKEY_SIZE];
} Auth02_t;

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	Auth02_t auth02;
} CommandPacket_Auth02_t;

typedef PACKED_STRUCT {
	bool isSuccess;
} Auth03_t;


#define RESPONSE_PACKET_PAYLOAD_SIZE	(30 - 1)	// -1 for cmdType. WARNING: Ensure NRF_BLE_GQ_DATAPOOL_ELEMENT_SIZE & NRF_BLE_GQ_GATTS_HVX_MAX_DATA_LEN are set to same value as sizeof(ResponsePacket_t). Don't forget about cmdType byte being figured into overall size being written to characteristic.

typedef PACKED_STRUCT {
	CmdType_t cmdType;
	uint8_t data[RESPONSE_PACKET_PAYLOAD_SIZE];
} ResponsePacket_t;

void commandsInit(void);
bool isCmdAllowed(CmdType_t cmdType);
void processCommand(void* p_event_data, uint16_t event_size);
void sendDataResponsePacket(CmdType_t cmdType, uint8_t *data, uint8_t length);
void sendDeviceInfo(void);
void sendName(void);
void sendSessionTimeout(void);
void noBleActivity_start(void);
void noBleActivity_stop(void);
void Cmd_SetGrip(void *p_event_data, uint16_t event_size);
void wristOnly(void);
void detectDeviceType(void);

#endif // COMMANDS_H
