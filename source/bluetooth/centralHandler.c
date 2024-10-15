
#include "centralHandler.h"

#include "app_scheduler.h"
#include "app_timer.h"
#include "gpio.h"
#include "ble.h"
#include "ble_conn_state.h"
#include "ble_db_discovery.h"
#include "ble_gap.h"
#include "nrf_ble_scan.h"
#include "ble_srv_common.h"
#include "bleInitialize.h"
#include "commands.h"
#include "fds.h"
#include "flashHandler.h"
#include "nrf_log.h"
#include "nrf_sdh_ble.h"
#include "popTimer.h"
#include "peer_manager.h"
#include "peer_manager_types.h"
#include "utility.h"


// Module Init
BLE_DB_DISCOVERY_DEF(discoverGatt);
NRF_BLE_SCAN_DEF(scanModule);

// Public variables

// Private variables
#define SCAN_INTERVAL		160		// In units of 0.625 millisecond.
#define SCAN_WINDOW			80		// In units of 0.625 millisecond.
ble_gap_scan_params_t scanParams = {
	.extended = 0,		// Not very many iOS/Android devices support this yet. Even ones that claim Bluetooth 5 support.
    .active = 1,
    .interval = SCAN_INTERVAL,
    .window = SCAN_WINDOW,
    .timeout = 0};
static ble_db_discovery_evt_t lastGATT;	// Put here because too large to put on scheduler x30.

// Private Function Declarations
static void centralScanInit(void);
static void scanEventHandler(scan_evt_t const *p_scan_evt);
static void centralPeerManagerInit(void);
static void centralPmEvtHandler(pm_evt_t const *p_evt);
static void centralDidDiscoveryGATT(ble_db_discovery_evt_t *p_evt);
static void centralDidDiscoveryGATT_AppContext(void *p_event_data, uint16_t event_size);
static void centralDidConnectToPeripheral(uint8_t const *addr, uint16_t connectionHandle);
static void centralDidDisconnectFromDevice(uint16_t connectionHandle);
static void centralDidTimeout(ble_gap_evt_t *p_ble_evt);
static void centralParamUpdateRequest(ble_gap_evt_t *p_gap_evt);
static void centralReceivedWriteResponse(ble_gattc_evt_t *gattc_evt);
static void centralReceivedReadResponse(uint16_t connection, ble_evt_t *p_ble_evt);
static void centralDidRead_someChar(ble_evt_t *p_ble_evt);
static void centralReceivedNotifyOfChange(uint16_t connection, ble_evt_t *p_ble_evt);
static void centralRead_Characteristic(uint16_t handle, uint16_t connectionHandle);
static void centralSendToCharacteristic(uint16_t handle, uint16_t connectionHandle, uint8_t *data, uint16_t length);


// Private Function Definitions
static void centralScanInit(void) {
	// Scan/discover devices.
	ret_code_t err_code;
	nrf_ble_scan_init_t scanInit;

	memset(&scanInit, 0, sizeof(scanInit));
	scanInit.p_scan_param = &scanParams;
	scanInit.connect_if_match = false;
	scanInit.p_conn_param = &connectionParameters;
	scanInit.conn_cfg_tag = APP_BLE_CONN_CFG_TAG_CENTRAL;

	err_code = nrf_ble_scan_init(&scanModule, &scanInit, scanEventHandler);
	APP_ERROR_CONTINUE(err_code);

	// Note: Filter count allowed determined by NRF_BLE_SCAN_UUID_CNT & NRF_BLE_SCAN_NAME_CNT in sdk_config.h
	setScanFilter();

	// Enable. NOTE: Advert & Scan Response packets are seen as 2 different packets during filtering. So if use multiple filters but AD Types are in different packets (ie advert vs scan rsp), then NO packets will be found.
	// err_code = nrf_ble_scan_filters_enable(&scanModule, NRF_BLE_SCAN_NAME_FILTER, false);
	// err_code = nrf_ble_scan_filters_enable(&scanModule, NRF_BLE_SCAN_UUID_FILTER, false);
	// err_code = nrf_ble_scan_filters_enable(&scanModule, NRF_BLE_SCAN_APPEARANCE_FILTER, false);
	err_code = nrf_ble_scan_filters_enable(&scanModule, (NRF_BLE_SCAN_NAME_FILTER | NRF_BLE_SCAN_APPEARANCE_FILTER), false);
	APP_ERROR_CONTINUE(err_code);
}

static void scanEventHandler(scan_evt_t const *p_scan_evt) {
	// WARNING: CALLED IN INTERRUPT CONTEXT!!
	static uint32_t lastAdvertTick = 0;
	ret_code_t err_code;

	switch (p_scan_evt->scan_evt_id) {
		case NRF_BLE_SCAN_EVT_FILTER_MATCH: {	// Filter(s) matched for scan data
			//NRF_LOG_INFO("NRF_BLE_SCAN_EVT_FILTER_MATCH");
			receivedBleScanResponse(p_scan_evt->params.filter_match);
		}
		break;

		case NRF_BLE_SCAN_EVT_WHITELIST_REQUEST:
			NRF_LOG_INFO("NRF_BLE_SCAN_EVT_WHITELIST_REQUEST");
		break;

		case NRF_BLE_SCAN_EVT_WHITELIST_ADV_REPORT:
			NRF_LOG_INFO("NRF_BLE_SCAN_EVT_WHITELIST_ADV_REPORT");
		break;

		case NRF_BLE_SCAN_EVT_NOT_FOUND:	// Filter(s) did not match for scan data
			//NRF_LOG_INFO("NRF_BLE_SCAN_EVT_NOT_FOUND");
		break;

		case NRF_BLE_SCAN_EVT_SCAN_TIMEOUT:
			NRF_LOG_INFO("NRF_BLE_SCAN_EVT_SCAN_TIMEOUT");
			centralStartScan();
		break;

		case NRF_BLE_SCAN_EVT_CONNECTING_ERROR:
			NRF_LOG_INFO("NRF_BLE_SCAN_EVT_CONNECTING_ERROR");
			err_code = p_scan_evt->params.connecting_err.err_code;
			APP_ERROR_CONTINUE(err_code);
		break;

		case NRF_BLE_SCAN_EVT_CONNECTED: {
			NRF_LOG_INFO("NRF_BLE_SCAN_EVT_CONNECTED");
			//ble_gap_evt_connected_t const *p_connected = p_scan_evt->params.connected.p_connected;
			// Scan is automatically stopped by the connection.
		} break;

		default:
			NRF_LOG_INFO("Scan Event Unimplemented 0x%04x", p_scan_evt->scan_evt_id);
			break;
	}
}

static void centralPeerManagerInit(void) {
	ble_gap_sec_params_t sec_param;
	ret_code_t err_code;

	err_code = pm_init();
	APP_ERROR_CONTINUE(err_code);

	memset(&sec_param, 0, sizeof(ble_gap_sec_params_t));

	// Security parameters to be used for all security procedures.
	sec_param.bond = SEC_PARAM_BOND;
	sec_param.mitm = SEC_PARAM_MITM;
	sec_param.lesc = SEC_PARAM_LESC;
	sec_param.keypress = SEC_PARAM_KEYPRESS;
	sec_param.io_caps = SEC_PARAM_IO_CAPABILITIES;
	sec_param.oob = SEC_PARAM_OOB;
	sec_param.min_key_size = SEC_PARAM_MIN_KEY_SIZE;
	sec_param.max_key_size = SEC_PARAM_MAX_KEY_SIZE;
	sec_param.kdist_own.enc = 0;
	sec_param.kdist_own.id = 0;
	sec_param.kdist_peer.enc = 0;
	sec_param.kdist_peer.id = 0;

	err_code = pm_sec_params_set(&sec_param);
	APP_ERROR_CONTINUE(err_code);

	err_code = pm_register(centralPmEvtHandler);
	APP_ERROR_CONTINUE(err_code);
}

static void centralPmEvtHandler(pm_evt_t const *p_evt) {
	ret_code_t err_code;

	switch (p_evt->evt_id) {
		case PM_EVT_BONDED_PEER_CONNECTED:
			NRF_LOG_INFO("Connected to a previously bonded device.");
			break;

		case PM_EVT_CONN_SEC_SUCCEEDED:
			NRF_LOG_INFO("Connection secured: role: %d, conn_handle: 0x%x, procedure: %d.",
			    ble_conn_state_role(p_evt->conn_handle), p_evt->conn_handle, p_evt->params.conn_sec_succeeded.procedure);
			break;

		case PM_EVT_CONN_SEC_FAILED:
			/* Often, when securing fails, it shouldn't be restarted, for security reasons.
			 * Other times, it can be restarted directly.
			 * Sometimes it can be restarted, but only after changing some Security Parameters.
			 * Sometimes, it cannot be restarted until the link is disconnected and reconnected.
			 * Sometimes it is impossible, to secure the link, or the peer device does not support it.
			 * How to handle this error is highly application dependent. */
			break;

		case PM_EVT_CONN_SEC_CONFIG_REQ: {
			// Reject pairing request from an already bonded peer.
			pm_conn_sec_config_t conn_sec_config;
			conn_sec_config.allow_repairing = false;
			pm_conn_sec_config_reply(p_evt->conn_handle, &conn_sec_config);
		} break;

		case PM_EVT_STORAGE_FULL:
			// Run garbage collection on the flash.
			err_code = fds_gc();
			if (err_code == FDS_ERR_BUSY || err_code == FDS_ERR_NO_SPACE_IN_QUEUES) {
				// Retry.
			} else {
				APP_ERROR_CONTINUE(err_code);
			}
			break;

		case PM_EVT_PEERS_DELETE_SUCCEEDED:
			NRF_LOG_INFO("PM_EVT_PEERS_DELETE_SUCCEEDED");
			centralStartScan();
			break;

		case PM_EVT_LOCAL_DB_CACHE_APPLY_FAILED:
			// The local database has likely changed, send service changed indications.
			pm_local_database_has_changed();
			break;

		case PM_EVT_PEER_DATA_UPDATE_FAILED:
			// Assert.
			APP_ERROR_CONTINUE(p_evt->params.peer_data_update_failed.error);
			break;

		case PM_EVT_PEER_DELETE_FAILED:
			// Assert.
			APP_ERROR_CONTINUE(p_evt->params.peer_delete_failed.error);
			break;

		case PM_EVT_PEERS_DELETE_FAILED:
			// Assert.
			APP_ERROR_CONTINUE(p_evt->params.peers_delete_failed_evt.error);
			break;

		case PM_EVT_ERROR_UNEXPECTED:
			// Assert.
			APP_ERROR_CONTINUE(p_evt->params.error_unexpected.error);
			break;

		case PM_EVT_CONN_SEC_START:
		case PM_EVT_PEER_DATA_UPDATE_SUCCEEDED:
		case PM_EVT_PEER_DELETE_SUCCEEDED:
		case PM_EVT_LOCAL_DB_CACHE_APPLIED:
		case PM_EVT_SERVICE_CHANGED_IND_SENT:
		case PM_EVT_SERVICE_CHANGED_IND_CONFIRMED:
		default:
			break;
	}
}

static void centralDidDiscoveryGATT(ble_db_discovery_evt_t *p_evt) {
	memcpy(&lastGATT, p_evt, sizeof(ble_db_discovery_evt_t));
	ret_code_t err_code = app_sched_event_put(NULL, 0, centralDidDiscoveryGATT_AppContext);
	APP_ERROR_CONTINUE(err_code);

}

static void centralDidDiscoveryGATT_AppContext(void *p_event_data, uint16_t event_size) {
	// Function for handling Service and Characteristic discovery events. This function is callback function to handle events from the database discovery module. Depending on the UUIDs that are discovered, this function should forward the events to their respective services.
	bool isComplete = false;

	// See if complete, else return
	switch (lastGATT.evt_type) {
		case BLE_DB_DISCOVERY_COMPLETE:
			NRF_LOG_INFO("BLE_DB_DISCOVERY_COMPLETE");
			isComplete = true;
		break;
		case BLE_DB_DISCOVERY_ERROR:
			NRF_LOG_INFO("BLE_DB_DISCOVERY_ERROR");
		break;
		case BLE_DB_DISCOVERY_SRV_NOT_FOUND:
			NRF_LOG_INFO("BLE_DB_DISCOVERY_SRV_NOT_FOUND");
		break;
		case BLE_DB_DISCOVERY_AVAILABLE:	// Means this discovery module instance is available for reuse.
			NRF_LOG_INFO("BLE_DB_DISCOVERY_AVAILABLE");
		break;
	}

	// Close discovery instance to reset registered services.
	ble_db_discovery_close(&discoverGatt);

	// Restart scanning
	centralStartScan();

	if (isComplete) {
		// // See which device type
		// if ((lastGATT.params.discovered_db.srv_uuid.uuid == someServiceUUID.uuid) && (lastGATT.params.discovered_db.srv_uuid.type == someServiceUUID.type)) {
		// 	someDiscoveredCharacteristics(&lastGATT.params.discovered_db);
		// }
	}
}

static void centralDidConnectToPeripheral(uint8_t const *addr, uint16_t connectionHandle) {
	// This Central connected to another device.
	// Scanning auto disables after a connection is made. So don't need to call centralStopScan();

	NRF_LOG_INFO("Central Connected to %02x%02x%02x%02x%02x%02x",
	    addr[0], addr[1], addr[2], addr[3], addr[4], addr[5]);

	// // Init GATT discovery module, BEFORE registering Service UUID(s) to discovery.
	// ret_code_t err_code = ble_db_discovery_init(centralDidDiscoveryGATT);
	// APP_ERROR_CONTINUE(err_code);

	// // See which device type
	// if (!memcmp(addr, someDevice.gapAddr.addr, BLE_GAP_ADDR_LEN)) {
	// 	someDeviceConnected(connectionHandle);
	// }

	// // Discover device's registered services.
	// NRF_LOG_INFO("Start Gatt Discovery");
	// memset(&discoverGatt, 0, sizeof(discoverGatt));
	// err_code = ble_db_discovery_start(&discoverGatt, connectionHandle);
	// APP_ERROR_CONTINUE(err_code);

	// Don't restart scanning until GATT discovery process is done or timeout.
}

static void centralDidDisconnectFromDevice(uint16_t connectionHandle) {
	// Called when the device is disconnected.

	// Which device?
	// if (someDevice.connectionHandle == connectionHandle) {
	// 	someDeviceDisconnected();
	// }

	// Restart discovery
	centralStartScan();
}

static void centralDidTimeout(ble_gap_evt_t *p_gap_evt) {
	// We have not specified a timeout for scanning, so only connection attemps can timeout.
	if (p_gap_evt->params.timeout.src == BLE_GAP_TIMEOUT_SRC_CONN) {
		NRF_LOG_INFO("Connection Request timed out.");
	}
}

static void centralParamUpdateRequest(ble_gap_evt_t *p_gap_evt) {
	// Accept parameters requested by peer.
	ret_code_t err_code = sd_ble_gap_conn_param_update(p_gap_evt->conn_handle, &p_gap_evt->params.conn_param_update_request.conn_params);
	APP_ERROR_CONTINUE(err_code);
}

static void centralReceivedWriteResponse(ble_gattc_evt_t *gattc_evt) {
	// The connected device responded with a write response.
	uint16_t connection = gattc_evt->conn_handle;
	NRF_LOG_INFO("WRITE_RSP connection: %d", connection);

	ble_gattc_evt_write_rsp_t *writeRsp = (ble_gattc_evt_write_rsp_t *)&gattc_evt->params.write_rsp;

	// Was it successful?
	if ((gattc_evt->gatt_status != BLE_GATT_STATUS_SUCCESS) || (writeRsp->write_op == BLE_GATT_OP_INVALID)) {
		NRF_LOG_INFO("Invalid GATT Operation during Write, Status %d, WriteOp: %d", gattc_evt->gatt_status, writeRsp->write_op);
		disconnectFromDevice(connection);
	}

	uint8_t *data = gattc_evt->params.hvx.data;
	uint8_t length = gattc_evt->params.hvx.len;
	uint16_t characteristicHandle = gattc_evt->params.write_rsp.handle;
	uint16_t status = gattc_evt->gatt_status;

	// Which device?
	// if (someDevice.connectionHandle == connection) {
	// 	someDeviceReceivedWriteResponse(characteristicHandle, status);
	// }
}

static void centralReceivedReadResponse(uint16_t connection, ble_evt_t *p_ble_evt) {
	// This is called in response to a sd_ble_gattc_read call on a peripheral device.

	// Which characteristic?
	uint16_t characteristicHandle = p_ble_evt->evt.gattc_evt.params.read_rsp.handle;

	centralDidRead_someChar(p_ble_evt);
}

static void centralDidRead_someChar(ble_evt_t *p_ble_evt) {
	// Central did a sd_ble_gattc_read on a characteristic and received it from the other device.
	NRF_LOG_INFO("Length = %d", p_ble_evt->evt.gattc_evt.params.read_rsp.len);

	switch (p_ble_evt->evt.gattc_evt.gatt_status) {
		// Successful read, some data was returned so copy to paylaod buffer to start building payload
		case BLE_GATT_STATUS_SUCCESS: {
			uint8_t *data = p_ble_evt->evt.gattc_evt.params.read_rsp.data;
			uint8_t length = p_ble_evt->evt.gattc_evt.params.read_rsp.len;

			// Do something with the data, if needed.
			// app_sched_event_put((NULL, 0, centralRead_someChar);
		} break;
		case BLE_GATT_STATUS_ATTERR_INVALID_OFFSET:
			break;
		default:
			break;
	}
}

static void centralReceivedNotifyOfChange(uint16_t connection, ble_evt_t *p_ble_evt) {
	// The connected device notified our central of value change.
	ret_code_t err_code;

	// If server sends an indication, then send a Handle Value Confirmation to the GATT Server.
	if (p_ble_evt->evt.gattc_evt.params.hvx.type == BLE_GATT_HVX_INDICATION) {
		err_code = sd_ble_gattc_hv_confirm(p_ble_evt->evt.gattc_evt.conn_handle,
		    p_ble_evt->evt.gattc_evt.params.hvx.handle);
		APP_ERROR_CONTINUE(err_code);
	}

	// Get data
	uint16_t characteristicHandle = p_ble_evt->evt.gattc_evt.params.read_rsp.handle;
	uint8_t *data = p_ble_evt->evt.gattc_evt.params.hvx.data;
	uint8_t length = p_ble_evt->evt.gattc_evt.params.hvx.len;

	//NRF_LOG_INFO("Char Handle: %d, %s data: (length: %d)", characteristicHandle, ((p_ble_evt->evt.gattc_evt.params.hvx.type != BLE_GATT_HVX_NOTIFICATION) ? "Indication" : "Notification"), length);
	//NRF_LOG_INFO_ARRAY(data, length);

	// Which device?
	// if (someDevice.connectionHandle == connection) {
	// 	someDeviceReceivedData(data, length, characteristicHandle);
	// }
}

static void centralRead_Characteristic(uint16_t handle, uint16_t connectionHandle) {
	// Performs a read of a characteristic.
	sd_ble_gattc_read(connectionHandle, handle, 0);
}

static void centralSendToCharacteristic(uint16_t handle, uint16_t connectionHandle, uint8_t *data, uint16_t length) {
	// Write value to characteristic handle.
	// MUST PASS A STATIC data pointer to this function.

	// Ensure characteristic handle is set.
	if (handle == BLE_CONN_HANDLE_INVALID) {
		return;
	}

	ble_gattc_write_params_t const write_params = {
	    .write_op = BLE_GATT_OP_WRITE_REQ,
	    .flags = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
	    .handle = handle,
	    .offset = 0,
	    .len = length,
	    .p_value = data};

	// Debug print
	NRF_LOG_INFO("Send:");
	NRF_LOG_INFO_ARRAY(data, length);
	NRF_LOG_INFO("Connection %d, Char Handle %d", connectionHandle, handle);

	// Write to gatt
	uint32_t err_code = sd_ble_gattc_write(connectionHandle, &write_params);
	APP_ERROR_CONTINUE(err_code);
}

// Public Function Definitions
void centralInit(void) {
	// Initialize central.
	centralScanInit();
	centralPeerManagerInit();
}

void centralDidEvent(void *p_event_data, uint16_t event_size) {
	// Central role events.
	// NOTE: addr coming from p_gap_evt->params.connected.peer_addr.addr is not always valid. It depends on the p_ble_evt->header.evt_id. Beware!
	ble_evt_t *p_ble_evt = (ble_evt_t *)p_event_data;

	ble_gap_evt_t *p_gap_evt = &p_ble_evt->evt.gap_evt;
	uint16_t connection = BLE_CONN_HANDLE_INVALID;
	uint8_t const *addr = NULL;

	switch (p_ble_evt->header.evt_id) {
		case BLE_GAP_EVT_CONNECTED: // 0x10
			NRF_LOG_INFO("BLE_GAP_EVT_CONNECTED - central");
			addr = p_gap_evt->params.connected.peer_addr.addr;
			connection = p_gap_evt->conn_handle;
			centralDidConnectToPeripheral(addr, connection);
			break;
		case BLE_GAP_EVT_DISCONNECTED: // 0x11
			NRF_LOG_INFO("BLE_GAP_EVT_DISCONNECTED - central");
			// Have to use connection here, no addr available in ble_gap_evt_disconnected_t.
			connection = p_gap_evt->conn_handle;
			centralDidDisconnectFromDevice(connection);
			break;
		case BLE_GAP_EVT_CONN_PARAM_UPDATE: // 0x12
			NRF_LOG_INFO("BLE_GAP_EVT_CONN_PARAM_UPDATE");
			break;
		case BLE_GAP_EVT_TIMEOUT: // 0x1B
			NRF_LOG_INFO("BLE_GAP_EVT_TIMEOUT");
			centralDidTimeout(p_gap_evt);
			break;
		case BLE_GAP_EVT_ADV_REPORT: // 0x1D
			NRF_LOG_INFO("BLE_GAP_EVT_ADV_REPORT");
			// This isn't called when using the Scan Module
			break;
		case BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST: // 0x1F
			NRF_LOG_INFO("BLE_GAP_EVT_CONN_PARAM_UPDATE_REQUEST");
			centralParamUpdateRequest(p_gap_evt);
			break;
		case BLE_GAP_EVT_DATA_LENGTH_UPDATE: // 0x24
			NRF_LOG_INFO("BLE_GAP_EVT_DATA_LENGTH_UPDATE");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GATTC_EVT_TIMEOUT: // 0x3B
			// Disconnect on GATT Client timeout event.
			NRF_LOG_INFO("GATT Client Timeout.");
			connection = p_ble_evt->evt.gattc_evt.conn_handle;
			disconnectFromDevice(connection);
			break;
		case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST: // 0x55
			NRF_LOG_INFO("BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GATTS_EVT_TIMEOUT: //0x56
			// Disconnect on GATT Server timeout event.
			NRF_LOG_INFO("GATT Server Timeout.");
			connection = p_ble_evt->evt.gatts_evt.conn_handle;
			disconnectFromDevice(connection);
			break;
		case BLE_GATTC_EVT_WRITE_CMD_TX_COMPLETE:
			NRF_LOG_INFO("TX_COMPLETE");
			connection = p_ble_evt->evt.gattc_evt.conn_handle;
			disconnectFromDevice(connection);
			break;
		case BLE_GATTC_EVT_WRITE_RSP:
			// Have to use connection here, not addr.
			NRF_LOG_INFO("BLE_GATTC_EVT_WRITE_RSP");
			ble_gattc_evt_t *gattc_evt = &p_ble_evt->evt.gattc_evt;
			centralReceivedWriteResponse(gattc_evt);
			break;
		case BLE_GATTC_EVT_READ_RSP:
			// Have to use connection here, not addr.
			NRF_LOG_INFO("BLE_GATTC_EVT_READ_RSP");
			connection = p_ble_evt->evt.gattc_evt.conn_handle;
			centralReceivedReadResponse(connection, p_ble_evt);
			break;
		case BLE_GATTC_EVT_CHAR_VALS_READ_RSP:
			NRF_LOG_INFO("CHAR_VALS_READ_RSP");
			break;
		case BLE_GATTC_EVT_HVX:
			// Notification of change
			//NRF_LOG_INFO("BLE_GATTC_EVT_HVX");
			connection = p_ble_evt->evt.gattc_evt.conn_handle;
			centralReceivedNotifyOfChange(connection, p_ble_evt);
			break;
		case BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP: // 0x30
			NRF_LOG_INFO("BLE_GATTC_EVT_PRIM_SRVC_DISC_RSP");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GATTC_EVT_CHAR_DISC_RSP: // 0x32
			NRF_LOG_INFO("BLE_GATTC_EVT_CHAR_DISC_RSP");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GATTC_EVT_DESC_DISC_RSP: // 0x33
			NRF_LOG_INFO("BLE_GATTC_EVT_DESC_DISC_RSP");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		case BLE_GATTC_EVT_EXCHANGE_MTU_RSP: // 0x3A
			NRF_LOG_INFO("BLE_GATTC_EVT_EXCHANGE_MTU_RSP");
			// Don't handle. SDK's nrf_ble_gatt_on_ble_evt handles it.
			break;
		default:
			NRF_LOG_INFO("Central Event Unimplemented 0x%04x", p_ble_evt->header.evt_id);
			break;
	}
}

void centralStartScan(void) {
	sd_ble_gap_scan_stop();

	// Start scanning for devices.
	ret_code_t err_code = nrf_ble_scan_start(&scanModule);
	APP_ERROR_CONTINUE(err_code);
	NRF_LOG_INFO("Scanning Started");
}

void centralStopScan(void) {
	// Stop scanning for devices.
	sd_ble_gap_scan_stop();
	NRF_LOG_INFO("Scanning Stopped");
}

ret_code_t centralConnect(ble_gap_addr_t *gapAddr) {

	setDataLength(BLE_CONN_HANDLE_INVALID, DataLength_Central);

	ret_code_t err_code = sd_ble_gap_connect(gapAddr, &scanParams, &connectionParameters, APP_BLE_CONN_CFG_TAG_CENTRAL);
	if (err_code != NRF_SUCCESS) {
		NRF_LOG_INFO("Connection Request Failed, error %d\r\n", err_code);
		return err_code;
	}

	return err_code;
}

void centralRegisterForNotificationChange(uint16_t cccdHandle, uint16_t connectionHandle) {
	uint8_t gattc_value[4];

	gattc_value[0] = BLE_GATT_HVX_NOTIFICATION;
	gattc_value[1] = 0;

	ble_gattc_write_params_t const write_params = {
	    .write_op = BLE_GATT_OP_WRITE_REQ,
	    .flags = BLE_GATT_EXEC_WRITE_FLAG_PREPARED_WRITE,
	    .handle = cccdHandle,
	    .offset = 0,
	    .len = BLE_CCCD_VALUE_LEN,
	    .p_value = gattc_value};

	uint32_t err_code = sd_ble_gattc_write(connectionHandle, &write_params);
	if (err_code == NRF_ERROR_BUSY) {
		NRF_LOG_INFO("%s: NRF_ERROR_BUSY, try again", __FUNCTION__);
	} else {
		APP_ERROR_CONTINUE(err_code);
	}
}

void setScanFilter(void) {
	// Clear previous scan filters.
	ret_code_t err_code;
	err_code = nrf_ble_scan_all_filter_remove(&scanModule);
	APP_ERROR_CONTINUE(err_code);
	debugPrint("Removed all scan filters");

	// Ensure appSleepCmd is up to date
	sprintf(appSleepCmd, "PopSlp%05i", storage.groupId);

	// Note: Filter count allowed determined by NRF_BLE_SCAN_NAME_CNT in sdk_config.h
	err_code = nrf_ble_scan_filter_set(&scanModule, SCAN_NAME_FILTER, &appSleepCmd);
	APP_ERROR_CONTINUE(err_code);
	debugPrint("Added Scan Name filter: %s", appSleepCmd);

	switch (device) {
		case Device_CatcherGlove:
			err_code = nrf_ble_scan_filter_set(&scanModule, SCAN_APPEARANCE_FILTER, &catcherWristAppearance);
			APP_ERROR_CONTINUE(err_code);
			err_code = nrf_ble_scan_filter_set(&scanModule, SCAN_APPEARANCE_FILTER, &fielderGloveAppearance);
			APP_ERROR_CONTINUE(err_code);
			debugPrint("Added Scan Appearance filter");
			break;
		case Device_CatcherWrist:
		case Device_FielderGlove:
			err_code = nrf_ble_scan_filter_set(&scanModule, SCAN_APPEARANCE_FILTER, &catcherGloveAppearance);
			APP_ERROR_CONTINUE(err_code);
			debugPrint("Added Scan Appearance filter");
			break;
		default:
			break;
	}
}
