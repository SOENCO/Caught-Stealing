
#include "peripheralHandler.h"

#include "app_scheduler.h"
#include "ble.h"
#include "bleInitialize.h"
#include "bleServices.h"
#include "centralHandler.h"
#include "commands.h"
#include "dwCommon.h"
#include "gpio.h"
#include "nrf_log.h"
#include <nrfx.h>
#include "utility.h"


// Module Init
APP_TIMER_DEF(advert_timerId);
APP_TIMER_DEF(auth_timerId);

// Public variables

// Private variables
#define ADVERT_DELAY_MS		5000
#define AUTH_DELAY_MS		10000

// Private Function Declarations
static void peripheralSystemAttributeMissing(ble_evt_t * p_ble_evt);
static void peripheralDidConnectToCentral(ble_evt_t *p_ble_evt);
static void peripheralDidDisconnectFromCentral(ble_evt_t *p_ble_evt);
static void peripheralClearGattData(uint16_t connectionHandle);
static void peripheralReceivedSecParamsRequest(ble_evt_t *p_ble_evt);
static void peripheralReceivedWrite(ble_evt_t *p_ble_evt);
static void peripheralReceivedRwAuthRequest(ble_evt_t *p_ble_evt);
static void peripheralReceivedUserMemoryRequest(uint16_t connectHandle);
static void peripheralWriteHandler(ble_evt_t *p_ble_evt, bool needsWriteResponse);
static void peripheralReadAuthHandler(ble_evt_t *p_ble_evt);
static void peripheralResponseToWrite(ble_gatts_evt_write_t *p_evt_write, bool writeAllowed);
static void peripheralWriteTo_CCCD(ble_evt_t *p_ble_evt);
static void peripheralReceivedPhyUpdateRequest(uint16_t connectHandle);

// Timers
static void timersInit(void);
static void advertTimer_start(void);
static void advertTimer_stop(void);
static void advertTimer_handler(void *context);
static void advertTimer_handler_AppContext(void* p_event_data, uint16_t event_size);
static void authTimer_start(void);
static void authTimer_handler(void *context);
static void authTimer_handler_AppContext(void* p_event_data, uint16_t event_size);


// Private Function Definitions
static void peripheralSystemAttributeMissing(ble_evt_t * p_ble_evt) {
	// No system attributes have been stored.
	ret_code_t err_code = sd_ble_gatts_sys_attr_set(phoneDevice.connectionHandle, NULL, 0, 0);
	// Ignore this error since we are not using Bonding. APP_ERROR_RESET(err_code);
}

static void peripheralDidConnectToCentral(ble_evt_t *p_ble_evt) {
	// Connected to a central (phone).
	ble_gap_evt_t *p_gap_evt = &p_ble_evt->evt.gap_evt;
	uint8_t *addr = p_gap_evt->params.connected.peer_addr.addr;
	NRF_LOG_INFO("Peripheral Connected to %02x%02x%02x%02x%02x%02x (little endian first left byte)",
					  addr[0], addr[1], addr[2],
					  addr[3], addr[4], addr[5]);

	// Save connection handle. Peripheral role only has 1.
	phoneDevice.isConnected = true;
	phoneDevice.hasPasskey = false;
	isAuthed = phoneDevice.hasPasskey;
	phoneDevice.connectionHandle = p_gap_evt->conn_handle;
	memcpy(phoneDevice.addr, addr, BLE_GAP_ADDR_LEN);

	peripheralClearGattData(phoneDevice.connectionHandle);

	// Establish Queued Writes with this connection (long writes).
	ret_code_t err_code = assignConnHandleToQwr(phoneDevice.connectionHandle);
	APP_ERROR_RESET(err_code);

	// Ping activity timer
	noBleActivity_start();

	// Ensure proper data length
	if (getDataLength(phoneDevice.connectionHandle) != DataLength_Peripheral) {
		setDataLength(phoneDevice.connectionHandle, DataLength_Peripheral);
	}

	// Delay restarting advertising on non-connectable, until after connection has completed service discovery.
	advertTimer_start();

	// Start auth timer
	authTimer_start();
}

static void peripheralDidDisconnectFromCentral(ble_evt_t *p_ble_evt) {
	// Disconnected from central (phone).
  	ret_code_t err_code;

	// No longer connected to Phone.
	NRF_LOG_INFO("Peripheral disconnected");

	// Attempting to clear gatt after disconnect causes System error 12290 (Unknown error code).
	// peripheralClearGattData(phoneDevice.connectionHandle);

	// Clear phone device
	phoneDevice.isConnected = false;
	phoneDevice.hasPasskey = false;
	isAuthed = phoneDevice.hasPasskey;
	phoneDevice.connectionHandle = BLE_CONN_HANDLE_INVALID;
	memset(&phoneDevice.addr, 0, BLE_GAP_ADDR_LEN);
	phoneDevice.needsNotified_data = false;

	// Restart advertising
	advertTimer_stop();
	authTimer_stop();
	advertisingStart(false);
	centralStartScan();

	// Stop activity timer
	noBleActivity_stop();

	// Ensure next connection to phone uses correct data length. peripheralDidConnectToCentral checks & updates after connection, if needed but this will save connection time.
	setDataLength(BLE_CONN_HANDLE_INVALID, DataLength_Peripheral);

	isStreamingDistance = false;
	streamAccelerometer = false;
}

static void peripheralClearGattData(uint16_t connectionHandle) {
	// Initialize gatt value struct. Clear any existing data to prevent it from being read by unauthorized connection.
	ble_gatts_value_t gattsValue;
	uint8_t gattsDataBuffer[sizeof(ResponsePacket_t)] = {0};
	gattsValue.p_value = (void*)gattsDataBuffer;
	gattsValue.offset = 0;
	gattsValue.len = 0;

	ret_code_t err_code = sd_ble_gatts_value_set(connectionHandle, gattDataHandle.value_handle, &gattsValue);
	APP_ERROR_CONTINUE(err_code);
}

static void peripheralReceivedSecParamsRequest(ble_evt_t *p_ble_evt) {
	uint16_t connectionHandle = p_ble_evt->evt.gattc_evt.conn_handle;
	ret_code_t err_code = sd_ble_gap_sec_params_reply(connectionHandle, BLE_GAP_SEC_STATUS_PAIRING_NOT_SUPP, NULL, NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void peripheralReceivedWrite(ble_evt_t *p_ble_evt) {
	// The connected device (phone) wrote to this local GATT.
	ble_gatts_evt_write_t *writeEvent = &p_ble_evt->evt.gatts_evt.params.write;
	NRF_LOG_INFO("BLE_GATTS_EVT_WRITE op %d", writeEvent->op);

	if ((writeEvent->op != BLE_GATTS_OP_PREP_WRITE_REQ) &&
		(writeEvent->op != BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) &&
		(writeEvent->op != BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL)) {
		peripheralWriteHandler(p_ble_evt, false);
	}
}

static void peripheralReceivedRwAuthRequest(ble_evt_t *p_ble_evt) {
	// The connected device (phone) wrote to this local GATT, requires a Write Response back.
	ble_gatts_evt_rw_authorize_request_t *request = &p_ble_evt->evt.gatts_evt.params.authorize_request;

	switch (request->type) {
		case BLE_GATTS_AUTHORIZE_TYPE_READ:
			//NRF_LOG_INFO("BLE_GATTS_AUTHORIZE_TYPE_READ");
			peripheralReadAuthHandler(p_ble_evt);
			break;
		case BLE_GATTS_AUTHORIZE_TYPE_WRITE:
			{
				uint8_t writeOp = request->request.write.op;
				NRF_LOG_INFO("BLE_GATTS_AUTHORIZE_TYPE_WRITE: writeOp: %d", writeOp);
				if ((writeOp != BLE_GATTS_OP_PREP_WRITE_REQ) &&
					(writeOp != BLE_GATTS_OP_EXEC_WRITE_REQ_NOW) &&
					(writeOp != BLE_GATTS_OP_EXEC_WRITE_REQ_CANCEL)) {
					peripheralWriteHandler(p_ble_evt, true);
				}
			}
			break;
		case BLE_GATTS_AUTHORIZE_TYPE_INVALID:
			return;
		default:
			return;
	}
}

static void peripheralReceivedUserMemoryRequest(uint16_t connectHandle) {
	ret_code_t err_code = sd_ble_user_mem_reply(connectHandle, NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void peripheralWriteHandler(ble_evt_t *p_ble_evt, bool needsWriteResponse) {
	ble_gatts_evt_write_t *writeEvt;

	// Based on response required or not, determines writeEvt.
	if (needsWriteResponse) {
		ble_gatts_evt_rw_authorize_request_t *p_auth_req;
		p_auth_req = &p_ble_evt->evt.gatts_evt.params.authorize_request;
		writeEvt = &p_auth_req->request.write;
	} else {
		writeEvt = &p_ble_evt->evt.gatts_evt.params.write;
	}

	NRF_LOG_INFO("peripheralWriteHandler needsWriteResponse %d", needsWriteResponse);

	// Only check for write allowed if gattCommandHandle.
	bool writeAllowed = (writeEvt->handle != gattCommandHandle.value_handle) || isCmdAllowed(((CommandPacket_t *) writeEvt->data)->cmdType);
	NRF_LOG_INFO("writeAllowed: %d", writeAllowed);

	// Respond to Write if needed. BE SURE TO RESPOND BEFORE DOING ANOTHER BLE PROCESS (LIKE CALLING processCommand WHICH SENDS A NOTIFICATION OF CHANGE).
	if (needsWriteResponse) {
		peripheralResponseToWrite(writeEvt, writeAllowed);
	}

	// See which characteristic.
	if (writeEvt->handle == gattCommandHandle.value_handle) {
		if (writeAllowed) {
			app_sched_event_put((void*) writeEvt->data, writeEvt->len, processCommand);
		}
	} else {
		// See if a CCCD write
		peripheralWriteTo_CCCD(p_ble_evt);
	}
}

static void peripheralReadAuthHandler(ble_evt_t *p_ble_evt) {
	bool readAllowed = true;

	ble_gatts_rw_authorize_reply_params_t read_authorize_reply;
	memset(&read_authorize_reply, 0, sizeof(read_authorize_reply));
	read_authorize_reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;

	if (readAllowed) {
		read_authorize_reply.params.read.update = 0;
		read_authorize_reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
	} else {
		read_authorize_reply.params.read.gatt_status = BLE_GATT_STATUS_ATTERR_READ_NOT_PERMITTED;
	}

	ret_code_t err_code;
	do {
		err_code = sd_ble_gatts_rw_authorize_reply(phoneDevice.connectionHandle, &read_authorize_reply);
	} while (err_code == NRF_ERROR_BUSY);
}

static void peripheralResponseToWrite(ble_gatts_evt_write_t *p_evt_write, bool writeAllowed) {
	// Respond to Writes.

	NRF_LOG_INFO("peripheralResponseToWrite");

	ble_gatts_rw_authorize_reply_params_t write_authorize_reply;
	memset(&write_authorize_reply, 0, sizeof(write_authorize_reply));
	write_authorize_reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;

	if (writeAllowed) {
		write_authorize_reply.params.write.len = p_evt_write->len;
		write_authorize_reply.params.write.p_data = p_evt_write->data;
		write_authorize_reply.params.write.update = 1;
		write_authorize_reply.params.write.gatt_status = BLE_GATT_STATUS_SUCCESS;
	} else {
		write_authorize_reply.params.write.gatt_status = BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED;
	}

	ret_code_t err_code;
	do {
		err_code = sd_ble_gatts_rw_authorize_reply(phoneDevice.connectionHandle, &write_authorize_reply);
	} while (err_code == NRF_ERROR_BUSY);
}

static void peripheralWriteTo_CCCD(ble_evt_t *p_ble_evt) {
	// CCCD is how notification of changes are set.

	ble_gatts_evt_write_t *p_evt_write = &p_ble_evt->evt.gatts_evt.params.write;
	if ((p_evt_write->handle == gattDataHandle.cccd_handle) && (p_evt_write->len == 2)) {
		phoneDevice.needsNotified_data = ble_srv_is_notification_enabled(p_evt_write->data);
		NRF_LOG_INFO("Data cccd: %d", phoneDevice.needsNotified_data);
	}
}

static void peripheralReceivedPhyUpdateRequest(uint16_t connectHandle) {
	ble_gap_phys_t const phys =
	{
		.rx_phys = BLE_GAP_PHY_2MBPS, //BLE_GAP_PHY_AUTO,
		.tx_phys = BLE_GAP_PHY_2MBPS, //BLE_GAP_PHY_AUTO,
	};
	ret_code_t errCode = sd_ble_gap_phy_update(connectHandle, &phys);
	if (errCode != NRF_SUCCESS) {
		APP_ERROR_CONTINUE(errCode);
	}
}

// Timers
static void timersInit(void) {
	// Init timers
	uint32_t err_code = app_timer_create(&advert_timerId, APP_TIMER_MODE_SINGLE_SHOT, advertTimer_handler);
	APP_ERROR_CONTINUE(err_code);

	err_code = app_timer_create(&auth_timerId, APP_TIMER_MODE_SINGLE_SHOT, authTimer_handler);
	APP_ERROR_CONTINUE(err_code);
}

static void advertTimer_start(void) {
	app_timer_stop(advert_timerId);
	uint32_t err_code = app_timer_start(advert_timerId, APP_TIMER_TICKS(ADVERT_DELAY_MS), NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void advertTimer_stop(void) {
	app_timer_stop(advert_timerId);
}

static void advertTimer_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, advertTimer_handler_AppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void advertTimer_handler_AppContext(void* p_event_data, uint16_t event_size) {
	// In App Context

	// Restart advertising after connection has completed service discovery.
	advertisingStart(false);
	centralStartScan();	// Should already be scanning and a connection doesn't cause it to stop.
}

static void authTimer_start(void) {
	app_timer_stop(auth_timerId);
	uint32_t err_code = app_timer_start(auth_timerId, APP_TIMER_TICKS(AUTH_DELAY_MS), NULL);
	APP_ERROR_CONTINUE(err_code);
}

void authTimer_stop(void) {
	app_timer_stop(auth_timerId);
}

static void authTimer_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, authTimer_handler_AppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void authTimer_handler_AppContext(void* p_event_data, uint16_t event_size) {
	// In App Context
	// Double check hasPasskey to bypass possible race-conditions.
	if (!phoneDevice.hasPasskey) {
		debugPrint("Auth Timeout, disconnecting");
		disconnectFromDevice(phoneDevice.connectionHandle);
	}
}


// Public Function Definitions
void peripheralInit(void) {
	timersInit();
}

void peripheralDidEvent(void* p_event_data, uint16_t event_size) {
  	ble_evt_t* p_ble_evt = (ble_evt_t*)p_event_data;
	// Peripheral role events.

	switch (p_ble_evt->header.evt_id) {
		case BLE_GAP_EVT_CONNECTED:		// 0x10
			NRF_LOG_INFO("BLE_GAP_EVT_CONNECTED - peripheral");
			peripheralDidConnectToCentral(p_ble_evt);
			break;
		case BLE_GAP_EVT_DISCONNECTED:	// 0x11
			NRF_LOG_INFO("BLE_GAP_EVT_DISCONNECTED - peripheral");
			peripheralDidDisconnectFromCentral(p_ble_evt);
			break;
		case BLE_GAP_EVT_CONN_PARAM_UPDATE: // 0x12
			NRF_LOG_INFO("BLE_GAP_EVT_CONN_PARAM_UPDATE");
			break;
		case BLE_GAP_EVT_SEC_PARAMS_REQUEST: // 0x13
			NRF_LOG_INFO("BLE_GAP_EVT_SEC_PARAMS_REQUEST");
			peripheralReceivedSecParamsRequest(p_ble_evt);
			break;
		case BLE_GAP_EVT_AUTH_STATUS: // 0x19
			NRF_LOG_INFO("BLE_GAP_EVT_AUTH_STATUS");
			break;
		case BLE_GAP_EVT_TIMEOUT:		// 0x1B
			NRF_LOG_INFO("BLE_GAP_EVT_TIMEOUT");
			// Advertising timed out.
            advertisingStart(false);
			break;
		case BLE_GATTS_EVT_WRITE:		// 0x50
			NRF_LOG_INFO("BLE_GATTS_EVT_WRITE");
			peripheralReceivedWrite(p_ble_evt);
			break;
		case BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST:	// 0x51
			NRF_LOG_INFO("BLE_GATTS_EVT_RW_AUTHORIZE_REQUEST");
			peripheralReceivedRwAuthRequest(p_ble_evt);
			break;
		case BLE_GATTC_EVT_EXCHANGE_MTU_RSP:	// 0x3A
			NRF_LOG_INFO("BLE_GATTC_EVT_EXCHANGE_MTU_RSP");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
        case BLE_GAP_EVT_ADV_SET_TERMINATED:		// 0x26
			NRF_LOG_INFO("BLE_GAP_EVT_ADV_SET_TERMINATED");
            advertisingStart(false);
            break;
		case BLE_GATTC_EVT_TIMEOUT:	// 0x3B
			// Disconnect on GATT Client timeout event.
			NRF_LOG_INFO("BLE_GATTC_EVT_TIMEOUT");
			disconnectFromDevice(p_ble_evt->evt.gattc_evt.conn_handle);
			break;
		case BLE_GATTS_EVT_TIMEOUT:	//0x56
			// Disconnect on GATT Server timeout event.
			NRF_LOG_INFO("BLE_GATTS_EVT_TIMEOUT");
			disconnectFromDevice(p_ble_evt->evt.gatts_evt.conn_handle);
			break;
		case BLE_EVT_USER_MEM_REQUEST:	//0x01
			NRF_LOG_INFO("BLE_EVT_USER_MEM_REQUEST");
			peripheralReceivedUserMemoryRequest(p_ble_evt->evt.gap_evt.conn_handle);
			break;
		case BLE_EVT_USER_MEM_RELEASE:	//0x02
			NRF_LOG_INFO("BLE_EVT_USER_MEM_RELEASE");
			break;
		case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST:	// 0x55
			NRF_LOG_INFO("BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GATTS_EVT_HVN_TX_COMPLETE:		// 0x57
			//NRF_LOG_INFO("BLE_GATTS_EVT_HVN_TX_COMPLETE");
			break;
		case BLE_GAP_EVT_PHY_UPDATE_REQUEST:		// 0x21
			NRF_LOG_INFO("BLE_GAP_EVT_PHY_UPDATE_REQUEST");
			peripheralReceivedPhyUpdateRequest(p_ble_evt->evt.gap_evt.conn_handle);
			break;
		case BLE_GAP_EVT_PHY_UPDATE:		// 0x22
			NRF_LOG_INFO("BLE_GAP_EVT_PHY_UPDATE");
			break;
		case BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST:		// 0x23
			NRF_LOG_INFO("BLE_GAP_EVT_DATA_LENGTH_UPDATE_REQUEST");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GAP_EVT_DATA_LENGTH_UPDATE:		// 0x24
			NRF_LOG_INFO("BLE_GAP_EVT_DATA_LENGTH_UPDATE");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GATTS_EVT_SYS_ATTR_MISSING:		// 0x52
			NRF_LOG_INFO("BLE_GATTS_EVT_SYS_ATTR_MISSING");
			peripheralSystemAttributeMissing(p_ble_evt);
			break;
		default:
			NRF_LOG_INFO("Peripheral Event Unimplemented 0x%04x", p_ble_evt->header.evt_id);
			// No implementation needed.
			break;
	}
}

void writeToGatt(ble_gatts_char_handles_t *gattHandle, uint8_t *data, uint16_t length) {
	// Writes value to Gatt and notifies of change. Assumes length fits inside notification packet size, otherwise trimmed.

	// Setup gatt value struct
	static ble_gatts_value_t gattValue;
	gattValue.p_value = data;
	gattValue.len = length;
	gattValue.offset = 0;

	// See if can notify
	bool canNotify = false;
	if (phoneDevice.connectionHandle != BLE_CONN_HANDLE_INVALID) {
		if ((gattHandle->value_handle == gattDataHandle.value_handle) && phoneDevice.needsNotified_data) {
			canNotify = true;
		}
	}

	// Notify change
	ret_code_t err_code;
	if (canNotify) {
		// Set params
		ble_gatts_hvx_params_t hvx_params;
		memset(&hvx_params, 0, sizeof(hvx_params));
		hvx_params.handle = gattHandle->value_handle;
		hvx_params.type = BLE_GATT_HVX_NOTIFICATION;
		hvx_params.offset = gattValue.offset;
		hvx_params.p_len = &gattValue.len;
		hvx_params.p_data = gattValue.p_value;

		// Send notification data which also updates the gatt DB
		err_code = sd_ble_gatts_hvx(phoneDevice.connectionHandle, &hvx_params);

        if (err_code == NRF_ERROR_RESOURCES) {
			// Wait for BLE_GATTS_EVT_HVN_TX_COMPLETE.
			NRF_LOG_INFO("GATTS_HVX NRF_ERROR_RESOURCES");
			return;
		} else if (err_code != NRF_SUCCESS) {
			//APP_ERROR_CONTINUE(err_code);  Don't Error out - Occationally the phone has disconnect between the check for connection and this call
			NRF_LOG_INFO("GATTS_HVX Error 0x%0x", err_code);
			return;
		} else {
			//NRF_LOG_INFO("GATTS_HVX NRF_SUCCESS");
			return;
		}
	} else {
		// Since sd_ble_gatts_hvx not called, need to reload the gatt DB anyway.
		err_code = sd_ble_gatts_value_set(phoneDevice.connectionHandle, gattHandle->value_handle, &gattValue);
		APP_ERROR_RESET(err_code);
		return;
	}
}
