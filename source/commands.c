
#include "commands.h"

#include "app_error.h"
#include "app_timer.h"
#include "bleInitialize.h"
#include "centralHandler.h"
#include "gpio.h"
#include "flashHandler.h"
#include "nrf_log.h"
#include "peripheralAuthentication.h"
#include "peripheralHandler.h"
#include "popTimer_CatcherGlove.h"
#include "ungroupHandler.h"
#include "utility.h"
#include "version.h"

// Module Init
APP_TIMER_DEF(noBleActivity_timerId);

// Public variables


// Private variables
#define NO_ACTIVITY_MS	120000	// 2 minutes
static ResponsePacket_t rspPacket;
static bool isWristOnly = false;

// Private Function Declarations
static void timersInit(void);
static void noBleActivity_handler(void *p_context);

// Private Function Definitions
static void setGroupConfig(CommandPacket_GroupConfig_t *packet);


// Private Function Definitions
static void setGroupConfig(CommandPacket_GroupConfig_t *packet) {
	// Only allow group config if not currently grouped or by owner.

	if (storage.groupId != UNGROUPED_ID) {
		debugPrint("IGNORED Group Config. Already grouped with %d.", storage.groupId);
		return;
	}

	// This allows grouping devices to also set owner.
	storage.groupId = packet->groupId;
	storage.catcherGloveFICR = packet->catcherGloveFICR;
	storage.catcherWristFICR = packet->catcherWristFICR;
	storage.fielderGloveFICR = packet->fielderGloveFICR;

	debugPrint("Set Group ID: %d", storage.groupId);

	updateFlash();
	sendDeviceInfo();

	// Update scan filter
	setScanFilter();
}


// Timers
static void timersInit(void) {
	uint32_t err_code = app_timer_create(&noBleActivity_timerId, APP_TIMER_MODE_SINGLE_SHOT, noBleActivity_handler);
	APP_ERROR_CONTINUE(err_code);
}

static void noBleActivity_handler(void *p_context) {
	// If this is ever hit, then no BLE activity has occurred for a bit.

	debugPrint("No BLE Activity with Phone: Disconnect");
	disconnectFromDevice(phoneDevice.connectionHandle);
}


// Public Function Definitions
void commandsInit(void) {
	timersInit();
	detectDeviceType();
}

bool isCmdAllowed(CmdType_t cmdType) {
	// See if cmd is allowed. Returns true if auth related or has passkey.

	// See if Authorized
	if (!phoneDevice.hasPasskey) {
		// Not Authorized, see if Auth cmds.
		if ((cmdType != Cmd_AuthCentralRequest) &&
			(cmdType != Cmd_AuthPeripheralResponse) &&
			(cmdType != Cmd_AuthCentralResponse) &&
			(cmdType != Cmd_AuthPeripheralResult)) {
			// Not Auth command, exit.
			debugPrint("Cmd issued without Authentication: %d\r\n", cmdType);
			return false;
		}
	}
	return true;
}

void processCommand(void* p_event_data, uint16_t event_size) {
	// Parse command and call associated function.
	static CommandPacket_t cmdPacket;

	uint32_t safeLength = (event_size <= sizeof(CommandPacket_t)) ? event_size : sizeof(CommandPacket_t);
	memset((uint8_t*) &cmdPacket, 0, sizeof(CommandPacket_t));
	memcpy((uint8_t*) &cmdPacket, (uint8_t *)p_event_data, safeLength);

	debugPrint("");
	debugPrint("Received Command: %d, safeLength: %d, %d", cmdPacket.cmdType, safeLength, event_size);

	// Ping activity timer
	noBleActivity_start();

	// Which cmd
	switch (cmdPacket.cmdType) {
		case Cmd_Unknown:
		break;

		case Cmd_Distance:
			if (device == Device_CatcherGlove) {
				bool enabled = ((CommandPacket_Stream_t*) &cmdPacket)->enable;
				debugPrint("Stream Distance: %s", (enabled ? "Enabled" : "Disabled"));
				isStreamingDistance = enabled;
			}
		break;

		case Cmd_Accelerometer:
			streamAccelerometer = ((CommandPacket_Stream_t*) &cmdPacket)->enable;
		break;

		case Cmd_StartPopTimer: {
			// Sets timeout and starts session w/ ID.
			CommandPacket_StartPopTimer_t* start = (CommandPacket_StartPopTimer_t*) &cmdPacket;
			setSessionTimeout(start->sessionTimeout);
			startPopTimer_CatcherGlove(start->sessionID);
		}
		break;

		case Cmd_StopPopTimer:
			stopPopTimer();
		break;

		case Cmd_GroupConfig:
			setGroupConfig((CommandPacket_GroupConfig_t*) &cmdPacket);
		break;

		case Cmd_GetDeviceInfo:
			sendDeviceInfo();
			sendName();
			sendSessionTimeout();
		break;

		case Cmd_DebugInfo:
		break;

		case Cmd_NameChange:
			setName(cmdPacket.parameter, (safeLength - 1)); // Remove count for cmdType
		break;

		case Cmd_SessionTimeout:
			setSessionTimeout(((CommandPacket_SessionTimeout_t*) &cmdPacket)->sessionTimeout);
		break;

		case Cmd_UnGroup:
			unGroupViaBLE(((CommandPacket_Ungroup_t*) &cmdPacket));
		break;

		case Cmd_Owner:
			setOwner(((CommandPacket_Owner_t*) &cmdPacket)->ownerId);
		break;

		case Cmd_WristFielderReady:
			wristAndFielderReady_CatcherGlove(((CommandPacket_WristFielderReady_t*) &cmdPacket)->sessionID);
		break;

		case Cmd_AuthCentralRequest:
			peripheralAuth_step1();
		break;

		case Cmd_AuthCentralResponse:
			peripheralAuth_step2(&((CommandPacket_Auth02_t*) &cmdPacket)->auth02);
		break;

		default:
			NRF_LOG_ERROR("Unknown Command: %d", cmdPacket.cmdType);
		break;
	}

}

void sendDataResponsePacket(CmdType_t cmdType, uint8_t *data, uint8_t length) {
	if (!phoneDevice.isConnected) {
		NRF_LOG_INFO("send DataResponsePacketCmd: Phone not connected");
		return;
	}

	if (!isCmdAllowed(cmdType)) {
		NRF_LOG_INFO("Cmd not allowed to be sent: %d", cmdType);
		return;
	}

	// Form response packet
	memset(rspPacket.data, 0, RESPONSE_PACKET_PAYLOAD_SIZE);
	rspPacket.cmdType = cmdType;
	uint32_t safeLength = (length <= RESPONSE_PACKET_PAYLOAD_SIZE) ? length : RESPONSE_PACKET_PAYLOAD_SIZE;
	memcpy(rspPacket.data, data, safeLength);

	// Send to BLE
	uint8_t totalLength = sizeof(rspPacket.cmdType) + safeLength;
	writeToGatt(&gattDataHandle, (uint8_t *) &rspPacket, totalLength);

	// Ping activity timer
	noBleActivity_start();
}

void sendDeviceInfo(void) {
	DeviceInfo_t info = {0};
	info.deviceType = device;
	info.groupId = storage.groupId;
	info.isStreamingDistance = isStreamingDistance;
	info.catcherGloveFICR = storage.catcherGloveFICR;
	info.catcherWristFICR = storage.catcherWristFICR;
	info.fielderGloveFICR = storage.fielderGloveFICR;
	info.ownerId = storage.ownerId;

	sendDataResponsePacket(Cmd_GetDeviceInfo, (uint8_t*)&info, sizeof(info));
}

void sendName(void) {
	char *name = getDeviceName();
	uint8_t length = strnlen(name, DEVICE_NAME_LENGTH);
	sendDataResponsePacket(Cmd_NameChange, name, length);
}

void sendSessionTimeout(void) {
	sendDataResponsePacket(Cmd_SessionTimeout, (uint8_t*) &storage.sessionTimeout, sizeof(storage.sessionTimeout));
}

void noBleActivity_start(void) {
	app_timer_stop(noBleActivity_timerId);

	// Block activity timer if in active session.
	if (getIsRunning()) {
		return;
	}

	uint32_t err_code = app_timer_start(noBleActivity_timerId, APP_TIMER_TICKS(NO_ACTIVITY_MS), NULL);
	APP_ERROR_CONTINUE(err_code);
}

void noBleActivity_stop(void) {
	app_timer_stop(noBleActivity_timerId);
}

void wristOnly(void) {
	isWristOnly = true;
	device = Device_CatcherWrist;
	debugPrint("Device set to WRIST");
}

void detectDeviceType(void) {
	// Checks hardware for device selection. Note: Whether or not a Wrist device is determined during dw1000Reset while attempting DW comms.
	if (nrf_gpio_pin_read(FUNC_SEL)) {
		device = Device_CatcherGlove;	// Standard hardware configuration
		debugPrint("Device set to Catcher Glove");
	} else {
		device = Device_FielderGlove;
		debugPrint("Device set to Fielder Glove");
	}
}
