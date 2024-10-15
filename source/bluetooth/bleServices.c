
#include "bleServices.h"

#include "app_scheduler.h"
#include "ble.h"
#include "ble_bas.h"
#include "ble_dis.h"
#include "bleService_Battery.h"
#include "bleService_DFU.h"
#include "commands.h"
#include "nrf_ble_qwr.h"
#include "utility.h"
#include "version.h"


// Module Init
NRF_BLE_QWR_DEF(qwrModuleCfg);

// Public variables
ble_gatts_char_handles_t gattCommandHandle;
ble_gatts_char_handles_t gattDataHandle;
const uint16_t catcherGloveAppearance = 0x04CA; // (0x13 Control Device << 6) + (0xA)	// Appearance: Using for Scan filtering
const uint16_t catcherWristAppearance = 0x04CB; // (0x13 Control Device << 6) + (0xB)	// Appearance: Using for Scan filtering
const uint16_t fielderGloveAppearance = 0x04CC; // (0x13 Control Device << 6) + (0xC)	// Appearance: Using for Scan filtering

const ble_uuid128_t primaryServiceUUID128 = {
    .uuid128 = {
			// CB748DB1-9C4E-48BD-9AA3-F2DEFAA8C9E2
            0xE2, 0xC9, 0xA8, 0xFA, 0xDE, 0xF2, 0xA3, 0x9A,
            0xBD, 0x48, 0x4E, 0x9C, 0xB1, 0x8D, 0x74, 0xCB}};

ble_uuid_t primaryServiceUUID = {
    .uuid = 0x0000,
    .type = BLE_UUID_TYPE_UNKNOWN,
};

// Private variables
#define NRF_BLE_QWRS_MAX_RCV_SIZE 128
#define QWR_BUFF_SIZE 	NRF_BLE_QWRS_MAX_RCV_SIZE	//MAX_BLE_TX_SIZE

static uint8_t qwrModuleBuffer[QWR_BUFF_SIZE];
static uint8_t genericCharacteristicBuffer[1]; // Initial buffer for characteristics

ServiceCfg_t primaryService;

typedef struct {
	uint16_t receivedLength;
	uint8_t receivedData[NRF_BLE_QWRS_MAX_RCV_SIZE];
} qwrEvent_t;

// Private Function Declarations
static void service_DeviceInfoInit(void);
static void service_PrimaryInit(void);
static void characteristic_CommandInit(void);
static void characteristic_DataInit(void);
static uint16_t queuedWriteHandler(nrf_ble_qwr_t *p_qwr, nrf_ble_qwr_evt_t *p_evt);
static void serviceErrorHandler(uint32_t nrf_error);

// Private Function Definitions
static void service_DeviceInfoInit(void) {
	// Initialize Device Information Service.
	ret_code_t err_code;
	ble_dis_init_t dis_init;

	char bootloader[10];
	sprintf(bootloader, "%d", bootloaderVersion);

	memset(&dis_init, 0, sizeof(dis_init));
	ble_srv_ascii_to_utf8(&dis_init.manufact_name_str, (char *)MANUFACTURER_NAME);
	ble_srv_ascii_to_utf8(&dis_init.model_num_str, (char *)MODEL_NUMBER);
	ble_srv_ascii_to_utf8(&dis_init.hw_rev_str, (char *)HARWARE_REVISION);
	ble_srv_ascii_to_utf8(&dis_init.fw_rev_str, (char *)FIRMWARE_REVISION);
	ble_srv_ascii_to_utf8(&dis_init.sw_rev_str, bootloader);

	dis_init.dis_char_rd_sec = SEC_OPEN;

	err_code = ble_dis_init(&dis_init);
	APP_ERROR_CONTINUE(err_code);
}

static void service_PrimaryInit(void) {
	// Initialize Service.
	ret_code_t err_code;

	// Initialize Queued Write Module. The buffer assigned is for collecting the incoming data and is not associated with any specific characteristic. It can be used for several characteristics because only 1 queued write can occur at a time.
	nrf_ble_qwr_init_t qwrInit;
	qwrInit.mem_buffer.len = QWR_BUFF_SIZE;
	qwrInit.mem_buffer.p_mem = qwrModuleBuffer;
	qwrInit.error_handler = serviceErrorHandler;
	qwrInit.callback = queuedWriteHandler;

	err_code = nrf_ble_qwr_init(&qwrModuleCfg, &qwrInit);
	APP_ERROR_CONTINUE(err_code);

	// Initialize the Service
	memset(&primaryService, 0, sizeof(primaryService));
	primaryService.error_handler = serviceErrorHandler;
	primaryService.conn_handle = BLE_CONN_HANDLE_INVALID;
	// primaryService.service_handle  Filled by SDK or Radio

	// Add service UUID to system so it can be used when adding service itself.
	err_code = sd_ble_uuid_vs_add(&primaryServiceUUID128, &primaryServiceUUID.type);
	APP_ERROR_RESET(err_code);

	// Set primary service uuid
	primaryServiceUUID.uuid = (primaryServiceUUID128.uuid128[12] | (primaryServiceUUID128.uuid128[13] << 8));
	// primaryServiceUUID.type = set by SDK in sd_ble_uuid_vs_add

	//  Hand the service parameters to the SD
	err_code = sd_ble_gatts_service_add(BLE_GATTS_SRVC_TYPE_PRIMARY, &primaryServiceUUID, &primaryService.service_handle);
	APP_ERROR_RESET(err_code);
}

static void characteristic_CommandInit(void) {
	// Adds the Command characteristic to service
	ble_add_char_params_t params;
	ret_code_t err_code;

	// Initialize the add char parms
	memset(&params, 0, sizeof(params));
	params.uuid = CHAR_UUID_COMMAND;
	params.uuid_type = primaryServiceUUID.type;
	params.max_len = COMMAND_MAX_SIZE;
	params.init_len = sizeof(genericCharacteristicBuffer);
	params.p_init_value = genericCharacteristicBuffer;
	params.is_var_len = true;
	params.char_props.broadcast = true;
	params.char_props.read = false;
	params.char_props.write_wo_resp = true;
	params.char_props.write = true;
	params.char_props.notify = false;
	params.char_props.indicate = false;
	params.char_props.auth_signed_wr = false;
	params.char_ext_props.reliable_wr = false;
	params.char_ext_props.wr_aux = false;
	params.is_defered_read = false;
	params.is_defered_write = true;
	params.read_access = SEC_NO_ACCESS;
	params.write_access = SEC_OPEN;
	params.cccd_write_access = SEC_NO_ACCESS;
	params.is_value_user = false;
	params.p_user_descr = NULL;
	params.p_presentation_format = NULL;

	// Add the characteristic
	err_code = characteristic_add(primaryService.service_handle,
	    &params,
	    &gattCommandHandle);
	APP_ERROR_CONTINUE(err_code);

	// Add characteristic to the QWR service
	err_code = nrf_ble_qwr_attr_register(&qwrModuleCfg, gattCommandHandle.value_handle);
	APP_ERROR_CONTINUE(err_code);
}

static void characteristic_DataInit(void) {
	// Adds the Data characteristic to service
	ble_add_char_params_t params;
	ret_code_t err_code;

	// Initialize the add char parms
	memset(&params, 0, sizeof(params));
	params.uuid = CHAR_UUID_DATA;
	params.uuid_type = primaryServiceUUID.type;
	params.max_len = DATA_MAX_SIZE;
	params.init_len = sizeof(genericCharacteristicBuffer);
	params.p_init_value = genericCharacteristicBuffer;
	params.is_var_len = true;
	params.char_props.broadcast = true;
	params.char_props.read = true;
	params.char_props.write_wo_resp = false;
	params.char_props.write = false;
	params.char_props.notify = true;
	params.char_props.indicate = false;
	params.char_props.auth_signed_wr = false;
	params.char_ext_props.reliable_wr = false;
	params.char_ext_props.wr_aux = false;
	params.is_defered_read = true;
	params.is_defered_write = false;
	params.read_access = SEC_OPEN;
	params.write_access = SEC_NO_ACCESS;
	params.cccd_write_access = SEC_OPEN;
	params.is_value_user = false;
	params.p_user_descr = NULL;
	params.p_presentation_format = NULL;

	// Add the characteristic
	err_code = characteristic_add(primaryService.service_handle, &params, &gattDataHandle);
	APP_ERROR_CONTINUE(err_code);

	// Add characteristic to the QWR service
	err_code = nrf_ble_qwr_attr_register(&qwrModuleCfg, gattDataHandle.value_handle);
	APP_ERROR_CONTINUE(err_code);
}

static uint16_t queuedWriteHandler(nrf_ble_qwr_t *p_qwr, nrf_ble_qwr_evt_t *p_evt) {
	qwrEvent_t event;
	event.receivedLength = NRF_BLE_QWRS_MAX_RCV_SIZE;
	ret_code_t err_code = nrf_ble_qwr_value_get(p_qwr, p_evt->attr_handle, event.receivedData, &event.receivedLength);

	if (err_code != NRF_SUCCESS) {
		NRF_LOG_INFO("nrf_ble_qwr_value_get failed");
		return BLE_GATT_STATUS_ATTERR_INSUF_AUTHORIZATION;
	}

	// See if incoming data or completed/executed queued write.
	if (p_evt->evt_type == NRF_BLE_QWR_EVT_AUTH_REQUEST) {
		// Can check the received value before accepting the write. We accept all bytes.
		NRF_LOG_INFO("NRF_BLE_QWR_EVT_AUTH_REQUEST");
		// To reject event.receivedData[0] use: return BLE_GATT_STATUS_ATTERR_INSUF_AUTHORIZATION;
		return BLE_GATT_STATUS_SUCCESS;
	} else if (p_evt->evt_type == NRF_BLE_QWR_EVT_EXECUTE_WRITE) {
		// This is where the data is finished receiving.
		NRF_LOG_INFO("NRF_BLE_QWR_EVT_EXECUTE_WRITE");

		// Which characteristic
		if (p_evt->attr_handle == gattCommandHandle.value_handle) {
			// Only check for write allowed if gattCommandHandle.
			if (isCmdAllowed(((CommandPacket_t*) event.receivedData)->cmdType)) {
				uint32_t safeLength = (event.receivedLength <= sizeof(CommandPacket_t)) ? event.receivedLength : sizeof(CommandPacket_t);
				app_sched_event_put((void*) event.receivedData, safeLength, processCommand);
			} else {
				return BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED;
			}
		}
	}
	return BLE_GATT_STATUS_SUCCESS;
}

static void serviceErrorHandler(uint32_t nrf_error) {
	APP_ERROR_CONTINUE(nrf_error);
}

// Public Function Definitions
void servicesInit(void) {
	// Initialize all services
	ret_code_t err_code;
	ble_dis_init_t dis_init;

	service_DfuInit();
	service_DeviceInfoInit();
	service_BatteryInit();
	service_PrimaryInit();
	characteristic_CommandInit();
	characteristic_DataInit();
}

ret_code_t assignConnHandleToQwr(uint16_t connectionHandle) {
	return (nrf_ble_qwr_conn_handle_assign(&qwrModuleCfg, connectionHandle));
}
