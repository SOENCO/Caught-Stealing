
#include "bleInitialize.h"

#include "app_scheduler.h"
#include "app_timer.h"
#include "ble.h"
#include "ble_advertising.h"
#include "ble_bas.h"
#include "ble_conn_state.h"
#include "ble_conn_params.h"
#include "ble_dis.h"
#include "ble_gap.h"
#include "ble_radio_notification.h"
#include "bleServices.h"
#include "bleService_Battery.h"
#include "centralHandler.h"
#include "gpio.h"
#include "peripheralHandler.h"
#include "popTimer.h"
#include "popTimer_CatcherGlove.h"
#include "LIS2DW12.h"
#include "nrf_ble_gatt.h"
#include "nrf_sdh.h"
#include "nrf_sdh_soc.h"
#include "temperatureHandler.h"
#include "flashHandler.h"
#include "utility.h"


// Module Init
NRF_BLE_GATT_DEF(gattModule);

// Public variables
#define OPCODE_LENGTH 1 // Length of opcode inside a notification
#define HANDLE_LENGTH 2 // Length of handle inside a notification
ble_gap_conn_params_t connectionParameters;
ble_gap_addr_t localBleAddress;
char localBleAddressString[] = "000000000000";
uint16_t mtuPayloadSize = NRF_SDH_BLE_GATT_MAX_MTU_SIZE - OPCODE_LENGTH - HANDLE_LENGTH;
PhoneDevice_t phoneDevice = {
	.isConnected = false,
	.hasPasskey = false,
	.connectionHandle = BLE_CONN_HANDLE_INVALID,
	.addr = {0},
	.needsNotified_data = false,
};
bool isAuthed = false;

// Private variables
#define NUM_SERVICES_TO_ADVERTISE 1
static ble_uuid_t servicesToAdvertise[NUM_SERVICES_TO_ADVERTISE];

uint16_t peripheralMTU = 512;
uint16_t centralMTU = 247;

static ble_gap_conn_sec_mode_t sec_mode;
static ble_gap_adv_params_t advertisingParameters;

static MfgData_Session_t session = {0};
static MfgData_Stats_t stats = {0};
static MfgData_Version_t versionMfg = {
	.type = MfgDataType_Version,
	.version = (FIRMWARE_REVISION_INT & 0x00FFFFFF)
};
static MfgData_Info_t infoMfg = {
	.type = MfgDataType_Info,
	.temperatureCelsius = 0,
	.ownerId = UNOWNED_ID
};

static ble_advdata_manuf_data_t advertMfgData = {
	.company_identifier = 0x0000,			// TODO: Enter valid company id
	.data.p_data = (uint8_t*)&session,
	.data.size = sizeof(session)
};

static ble_advdata_manuf_data_t versionMfgData = {
	.company_identifier = 0x0000,			// TODO: Enter valid company id
	.data.p_data = (uint8_t*)&versionMfg,
	.data.size = sizeof(versionMfg)
};

static ble_advdata_manuf_data_t infoMfgData = {
	.company_identifier = 0x0000,			// TODO: Enter valid company id
	.data.p_data = (uint8_t*)&infoMfg,
	.data.size = sizeof(infoMfg)
};

static uint8_t advHandle = BLE_GAP_ADV_SET_HANDLE_NOT_SET;	// Advertising handle used to identify an advertising set. Is set by sd_ble_gap_adv_set_configure if BLE_GAP_ADV_SET_HANDLE_NOT_SET is passed.

// NOTE: In order to update advertising data while advertising, new advertising buffers must be provided, therefore have 2x buffer sets.
static uint8_t advData01[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static uint8_t srdData01[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static uint8_t advData02[BLE_GAP_ADV_SET_DATA_SIZE_MAX];
static uint8_t srdData02[BLE_GAP_ADV_SET_DATA_SIZE_MAX];

static ble_gap_adv_data_t advdataAndSrdata = {
	.adv_data = {
		.p_data = advData01,
		.len = BLE_GAP_ADV_SET_DATA_SIZE_MAX
	},
	.scan_rsp_data = {
		.p_data = srdData01,
		.len = BLE_GAP_ADV_SET_DATA_SIZE_MAX
	}
};

// Private Function Declarations
static void softDeviceConfig_Peripheral(uint32_t ramStart);
static void advertiseInit(void);
static ble_advdata_t getScanResponsePacket(ScanRespType_t type);
static void radioEvent(bool radio_active);
static void updateAdvertData(ble_advdata_t *advdata, ble_advdata_t *srdata);
static void gattEventHandler(nrf_ble_gatt_t *p_gatt, nrf_ble_gatt_evt_t const * p_evt);
static void bleEvtHandler(ble_evt_t const *p_ble_evt, void * p_context);
static void gattInit(void);
static void connParamsInit(void);
static void gapParamsInit(void);
static void bleInfoInit(void);
static void onConnectionParametersEventHandler(ble_conn_params_evt_t * p_evt);
static void onConnectionParametersErrorHandler(uint32_t nrf_error);
static void setConnectableParameters(bool connectable, bool isFastInterval);

// Private Function Definitions
static void softDeviceConfig_Peripheral(uint32_t ramStart) {
	ret_code_t err_code;
	ble_cfg_t bleCfg;

	// Setup memory for a max number of connections
	memset(&bleCfg, 0, sizeof(bleCfg));
	bleCfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG_PERIPHERAL;
	bleCfg.conn_cfg.params.gap_conn_cfg.conn_count = NRF_SDH_BLE_PERIPHERAL_LINK_COUNT;
	bleCfg.conn_cfg.params.gap_conn_cfg.event_length = BLE_GAP_EVENT_LENGTH_DEFAULT * 10;	// NOTE: when using Extended Data Length be sure to increase event_length beyond BLE_GAP_EVENT_LENGTH_DEFAULT or the data rate throughput will be greatly decreased (~26x factor).
	err_code = sd_ble_cfg_set(BLE_CONN_CFG_GAP, &bleCfg, ramStart);
	APP_ERROR_RESET(err_code);

	// Setup memory for MTU
	memset(&bleCfg, 0, sizeof(bleCfg));
    bleCfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG_PERIPHERAL;
    bleCfg.conn_cfg.params.gatt_conn_cfg.att_mtu = peripheralMTU;
    err_code = sd_ble_cfg_set(BLE_CONN_CFG_GATT, &bleCfg, ramStart);
	APP_ERROR_RESET(err_code);

	//  Setup memory for a max UUID count
	memset(&bleCfg, 0, sizeof(bleCfg));
	bleCfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG_PERIPHERAL;
	bleCfg.common_cfg.vs_uuid_cfg.vs_uuid_count = 4;	// BAS, PRIMARY, DFU, DIS
	err_code = sd_ble_cfg_set(BLE_COMMON_CFG_VS_UUID, &bleCfg, ramStart);
	APP_ERROR_RESET(err_code);

	// Setup memory for a max sized attribute table
	memset(&bleCfg, 0, sizeof(bleCfg));
	bleCfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG_PERIPHERAL;
	bleCfg.gatts_cfg.attr_tab_size.attr_tab_size = 3300; // Multiple of 4
	err_code = sd_ble_cfg_set(BLE_GATTS_CFG_ATTR_TAB_SIZE, &bleCfg, ramStart);
	APP_ERROR_RESET(err_code);

    // Configure HVN queue size
    memset(&bleCfg, 0, sizeof(bleCfg));
    bleCfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG_PERIPHERAL;
    bleCfg.conn_cfg.params.gatts_conn_cfg.hvn_tx_queue_size = 16;
    err_code = sd_ble_cfg_set(BLE_CONN_CFG_GATTS, &bleCfg, ramStart);
    APP_ERROR_RESET(err_code);

    // Configure Service Changed characteristic.
    memset(&bleCfg, 0x00, sizeof(bleCfg));
	bleCfg.conn_cfg.conn_cfg_tag = APP_BLE_CONN_CFG_TAG_PERIPHERAL;
    bleCfg.gatts_cfg.service_changed.service_changed = NRF_SDH_BLE_SERVICE_CHANGED;
    err_code = sd_ble_cfg_set(BLE_GATTS_CFG_SERVICE_CHANGED, &bleCfg, ramStart);
	APP_ERROR_RESET(err_code);
}

static void advertiseInit(void) {
	// Only call once at start of program.
	ret_code_t err_code;

	// Set handler so payloads can be changed between radio transmits.
	err_code = ble_radio_notification_init(APP_IRQ_PRIORITY_LOW, NRF_RADIO_NOTIFICATION_DISTANCE_2680US, radioEvent);

	// Configure advertising parameters
	setConnectableParameters(true, false);

	servicesToAdvertise[0] = primaryServiceUUID;

	ble_advdata_t advConfig;
	memset(&advConfig, 0, sizeof(advConfig));
	advConfig.name_type = BLE_ADVDATA_NO_NAME;
	advConfig.include_appearance = true;
	advConfig.flags = BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE;
	advConfig.uuids_complete.uuid_cnt = NUM_SERVICES_TO_ADVERTISE;
	advConfig.uuids_complete.p_uuids = servicesToAdvertise;

	ble_advdata_t srConfig = getScanResponsePacket(ScanRespType_Name);
	updateAdvertData(&advConfig, &srConfig);
}

static ble_advdata_t getScanResponsePacket(ScanRespType_t type) {
    ble_advdata_t srdata = {0};

	switch (type) {
		case ScanRespType_Name:
			srdata.include_appearance = false;
			srdata.p_manuf_specific_data = NULL;
			srdata.name_type = BLE_ADVDATA_FULL_NAME;

			// Add mfg data containing firmware version .
			srdata.p_manuf_specific_data = &versionMfgData;
		break;

		case ScanRespType_Session:
			// Including 'appearance' in both advert & scan response since Scan Filters scan per advert packet type (ie for regular advert & scan response).
			srdata.include_appearance = true;
			srdata.p_manuf_specific_data = &advertMfgData;
			srdata.name_type = BLE_ADVDATA_NO_NAME;

			// Get latest mfg data
			session.type = MfgDataType_Session;
			session.battery = getBatteryLevel();
			session.flags.isShaken = isShaken;
			session.flags.isAuthed = isAuthed;
			session.flags.isRunning = getIsRunning();
			session.flags.empty = 0;
			session.info = getSessionInfo();

			// Point to session data
			advertMfgData.data.p_data = (uint8_t*)&session;
			advertMfgData.data.size = sizeof(session);
		break;

		case ScanRespType_Stats:
			// Should only be called if a Catcher Glove device.

    		srdata.include_appearance = false;
			srdata.p_manuf_specific_data = &advertMfgData;
    		srdata.name_type = BLE_ADVDATA_NO_NAME;

			// Get latest mfg data
			stats.type = MfgDataType_Stat;
			stats.stats = getStats();

			// Point to stats data
			advertMfgData.data.p_data = (uint8_t*)&stats;
			advertMfgData.data.size = sizeof(stats);
		break;

		case ScanRespType_Info:
			srdata.include_appearance = false;
			srdata.p_manuf_specific_data = NULL;
			srdata.name_type = BLE_ADVDATA_NO_NAME;

			// Add mfg data containing firmware version .
			infoMfg.temperatureCelsius = getTemperature();
			infoMfg.ownerId = storage.ownerId;
			srdata.p_manuf_specific_data = &infoMfgData;
		break;
	}

	return srdata;
}

static void radioEvent(bool radio_active) {
	if (radio_active) {
		// Only updating the scan response packet.
		ble_advdata_t srConfig = getScanResponsePacket(getNextScanType());
		updateAdvertData(NULL, &srConfig);
	}
}

static void updateAdvertData(ble_advdata_t *advdata, ble_advdata_t *srdata) {
	// Swaps advert buffers.
	ret_code_t err_code;

	// Copy existing buffere into new. This ensure that if one of the passed advert data is NULL that we don't end up with an empty buffer being advertised.
	uint8_t *oldAdvData = advdataAndSrdata.adv_data.p_data;
	uint8_t *newAdvData = (advdataAndSrdata.adv_data.p_data == advData01) ? advData02 : advData01;
	memcpy(newAdvData, oldAdvData, sizeof(advData01));

	uint8_t *oldSrData = advdataAndSrdata.scan_rsp_data.p_data;
	uint8_t *newSrData = (advdataAndSrdata.scan_rsp_data.p_data == srdData01) ? srdData02 : srdData01;
	memcpy(newSrData, oldSrData, sizeof(srdData01));

	// Point to new buffers
	advdataAndSrdata.adv_data.p_data = newAdvData;
	advdataAndSrdata.scan_rsp_data.p_data = newSrData;

	// Update new buffer.
	if (advdata != NULL) {
		// Reset max length before passing to ble_advdata_encode is called since ble_advdata_encode changes it based on amount used.
		advdataAndSrdata.adv_data.len = BLE_GAP_ADV_SET_DATA_SIZE_MAX;
		err_code = ble_advdata_encode(advdata, advdataAndSrdata.adv_data.p_data, &advdataAndSrdata.adv_data.len);
		APP_ERROR_RESET(err_code);
	}

	// Update new buffer.
	if (srdata != NULL) {
		// Reset max length before passing to ble_advdata_encode is called since ble_advdata_encode changes it based on amount used.
		advdataAndSrdata.scan_rsp_data.len = BLE_GAP_ADV_SET_DATA_SIZE_MAX;
		err_code = ble_advdata_encode(srdata, advdataAndSrdata.scan_rsp_data.p_data, &advdataAndSrdata.scan_rsp_data.len);
		APP_ERROR_RESET(err_code);
	}

	// NOTE: While advertising, advert params passed to sd_ble_gap_adv_set_configure must be NULL.
	err_code = sd_ble_gap_adv_set_configure(&advHandle, &advdataAndSrdata, NULL);
	if (err_code != NRF_SUCCESS) {
		APP_ERROR_RESET(err_code);
	}
}

static void bleEvtHandler(ble_evt_t const *p_ble_evt, void * p_context) {
	ret_code_t err_code;
	uint16_t connectionHandle = p_ble_evt->evt.gap_evt.conn_handle;
	uint16_t role = ble_conn_state_role(connectionHandle);

	// Dispatch to the right handler based on the role for this connection.
	if (role == BLE_GAP_ROLE_PERIPH) {
	  	err_code = app_sched_event_put(p_ble_evt, sizeof(ble_evt_t), peripheralDidEvent);
		APP_ERROR_CONTINUE(err_code);
	} else if (role == BLE_GAP_ROLE_CENTRAL) {
		err_code = app_sched_event_put(p_ble_evt, sizeof(ble_evt_t), centralDidEvent);
		APP_ERROR_CONTINUE(err_code);
	}

	uint16_t space = app_sched_queue_space_get();
	if (space < 5) {
		NRF_LOG_INFO("WARNING: Scheduler Queue: %d remaining", space);
	}
}

static void gattEventHandler(nrf_ble_gatt_t * p_gatt, nrf_ble_gatt_evt_t const * p_evt) {
	uint16_t connection = p_evt->conn_handle;

	switch (p_evt->evt_id) {
		case NRF_BLE_GATT_EVT_ATT_MTU_UPDATED:
			mtuPayloadSize = p_evt->params.att_mtu_effective - OPCODE_LENGTH - HANDLE_LENGTH;
			NRF_LOG_INFO("MTU negotiated: %u, Effective Payload %u", p_evt->params.att_mtu_effective, mtuPayloadSize);
			break;

		case NRF_BLE_GATT_EVT_DATA_LENGTH_UPDATED:
			NRF_LOG_INFO("Data length updated to %u bytes.",  p_evt->params.data_length);
			break;
	}
}

static void gattInit(void) {
	ret_code_t err_code = nrf_ble_gatt_init(&gattModule, gattEventHandler);
	APP_ERROR_RESET(err_code);

	nrf_ble_gatt_att_mtu_periph_set(&gattModule, peripheralMTU);

	// Enable Data Length Extension
	// Data Length also must be set separately but is done on a connection or 'next' connection basis with nrf_ble_gatt_data_length_set, based on role is not supported. Calling here to set initial length.
	setDataLength(BLE_CONN_HANDLE_INVALID, DataLength_Peripheral);
}

static void connParamsInit(void) {
	ble_conn_params_init_t cp_init;

	memset(&cp_init, 0, sizeof(cp_init));
	cp_init.p_conn_params = NULL;
	cp_init.first_conn_params_update_delay = FIRST_CONN_PARAMS_UPDATE_DELAY;
	cp_init.next_conn_params_update_delay = NEXT_CONN_PARAMS_UPDATE_DELAY;
	cp_init.max_conn_params_update_count = MAX_CONN_PARAMS_UPDATE_COUNT;
	cp_init.start_on_notify_cccd_handle = BLE_CONN_HANDLE_INVALID; // Start upon connection.
	cp_init.disconnect_on_fail = true;
	cp_init.evt_handler = onConnectionParametersEventHandler;
	cp_init.error_handler = onConnectionParametersErrorHandler;

	ret_code_t err_code = ble_conn_params_init(&cp_init);
	APP_ERROR_RESET(err_code);
}

static void gapParamsInit(void) {
	ret_code_t err_code;

	BLE_GAP_CONN_SEC_MODE_SET_OPEN(&sec_mode);

	setGapName();
	setDeviceAppearance();

    memset(&connectionParameters, 0, sizeof(connectionParameters));

    connectionParameters.min_conn_interval = MIN_CONNECTION_INTERVAL;
    connectionParameters.max_conn_interval = MAX_CONNECTION_INTERVAL;
    connectionParameters.slave_latency     = SLAVE_LATENCY;
    connectionParameters.conn_sup_timeout  = SUPERVISION_TIMEOUT;

	err_code = sd_ble_gap_ppcp_set(&connectionParameters);
	APP_ERROR_RESET(err_code);

	// Enable Connection Event Extension
    ble_opt_t optionsConnExt;
    memset(&optionsConnExt, 0, sizeof(optionsConnExt));
    optionsConnExt.common_opt.conn_evt_ext.enable = 1;
    err_code = sd_ble_opt_set(BLE_COMMON_OPT_CONN_EVT_EXT, &optionsConnExt);
    APP_ERROR_RESET(err_code);
}

static void bleInfoInit(void) {
	// Get BLE Address from SoftDevice.
	ble_gap_addr_t address;
	uint32_t err_code = sd_ble_gap_addr_get(&address);
	APP_ERROR_RESET(err_code);

	// Copy to local var
	memcpy(&localBleAddress, &address, sizeof(ble_gap_addr_t));

	// Copy to string no colons, little endian starting at left position
	char *ptr = localBleAddressString;
	sprintf(ptr, "%02x%02x%02x%02x%02x%02x",
			localBleAddress.addr[0], localBleAddress.addr[1], localBleAddress.addr[2],
			localBleAddress.addr[3], localBleAddress.addr[4], localBleAddress.addr[5]);
}

static void onConnectionParametersEventHandler(ble_conn_params_evt_t * p_evt) {
    uint32_t err_code;

	NRF_LOG_INFO("onConnectionParametersEventHandler: %d", p_evt->evt_type);
    if (p_evt->evt_type == BLE_CONN_PARAMS_EVT_FAILED) {
        err_code = sd_ble_gap_disconnect(phoneDevice.connectionHandle, BLE_HCI_CONN_INTERVAL_UNACCEPTABLE);
        APP_ERROR_RESET(err_code);
    }
}

static void onConnectionParametersErrorHandler(uint32_t nrf_error) {
	NRF_LOG_INFO("onConnectionParametersErrorHandler: %d", nrf_error);
    APP_ERROR_HANDLER(nrf_error);
}

static void setConnectableParameters(bool connectable, bool isFastInterval) {
    // Configures connectable/non-connectable advertising.
	static bool lastConnectable = false;
	static bool lastIsFastInterval = true;
	ret_code_t err_code;

	// See if already advertising in correct mode.
	if ((lastConnectable == connectable) && (lastIsFastInterval == isFastInterval)) {
		return;
	}
	lastConnectable = connectable;
	lastIsFastInterval = isFastInterval;

	memset(&advertisingParameters, 0, sizeof(advertisingParameters));

	advertisingParameters.properties.type = (connectable) ? BLE_GAP_ADV_TYPE_CONNECTABLE_SCANNABLE_UNDIRECTED : BLE_GAP_ADV_TYPE_NONCONNECTABLE_SCANNABLE_UNDIRECTED;
	advertisingParameters.duration = BLE_GAP_ADV_TIMEOUT_GENERAL_UNLIMITED;
	advertisingParameters.p_peer_addr = NULL;
	advertisingParameters.filter_policy = BLE_GAP_ADV_FP_ANY;
	advertisingParameters.interval = (isFastInterval) ? CONNECTABLE_ADV_INTERVAL_20_MS : CONNECTABLE_ADV_INTERVAL_187_MS;
	advertisingParameters.primary_phy = BLE_GAP_PHY_AUTO;
	advertisingParameters.secondary_phy = BLE_GAP_PHY_AUTO;

	// Ignore stop error as it may not currently be advertising.
	sd_ble_gap_adv_stop(advHandle);

	NRF_LOG_INFO("Advert Params: %s", ((connectable) ? "Connectable" : "Non-connectable"));
	NRF_LOG_INFO("Advert Interval: %d", advertisingParameters.interval);

	ble_gap_adv_data_t *data = (connectable) ? &advdataAndSrdata : NULL;
    err_code = sd_ble_gap_adv_set_configure(&advHandle, data, &advertisingParameters);
	if (err_code != NRF_SUCCESS) {
	   APP_ERROR_CHECK(err_code);
	}

	// Call this AFTER sd_ble_gap_adv_set_configure to pass a valid advert handle.
	err_code = sd_ble_gap_tx_power_set(BLE_GAP_TX_POWER_ROLE_ADV, advHandle, TX_POWER_LEVEL);
	APP_ERROR_RESET(err_code);
}


// Public Function Definitions
void bleStackInit(void) {
	ret_code_t err_code;
	uint32_t ramStart = 0;
	ble_cfg_t bleCfg;

	err_code = nrf_sdh_enable_request();
	APP_ERROR_RESET(err_code);

	// Get start address of the application RAM.
    err_code = nrf_sdh_ble_app_ram_start_get(&ramStart);

	// Setup memory for a max number of connections.
	memset(&bleCfg, 0, sizeof(bleCfg));
	bleCfg.gap_cfg.role_count_cfg.periph_role_count = NRF_SDH_BLE_PERIPHERAL_LINK_COUNT;
	bleCfg.gap_cfg.role_count_cfg.central_role_count = NRF_SDH_BLE_CENTRAL_LINK_COUNT;
	bleCfg.gap_cfg.role_count_cfg.central_sec_count = 0;
	err_code = sd_ble_cfg_set(BLE_GAP_CFG_ROLE_COUNT, &bleCfg, ramStart);
	APP_ERROR_RESET(err_code);

	// Add separate Soft Device configs for peripheral and central to greatly reduce SD RAM usage with multiple central links.
	softDeviceConfig_Peripheral(ramStart);

	// Enable BLE stack.
	err_code = nrf_sdh_ble_enable(&ramStart);
	APP_ERROR_RESET(err_code);

	// Register a handler for BLE events.
	NRF_SDH_BLE_OBSERVER(m_ble_observer, APP_BLE_OBSERVER_PRIO, bleEvtHandler, NULL);
}

void bleInit(void) {
	gapParamsInit();
	bleInfoInit();
	gattInit();
	servicesInit();
	advertiseInit();
	connParamsInit();
	centralInit();
	advertisingStart(false);	// Be sure to start advertising last. This prevents other centrals from initiating a connection before everything is ready and no conflicts with other GATT processes.
}

void advertisingStart(bool isFastInterval) {
	ret_code_t err_code;

	// Ignore stop error as it may not currently be advertising.
	sd_ble_gap_adv_stop(advHandle);

	// Setup the new advertising parameters
	setConnectableParameters(!phoneDevice.isConnected, isFastInterval);

	// Force reload of advertising data - passing true forces reload.
	radioEvent(true);

	// Start the advertising with new data
	NRF_LOG_INFO("Start Advertising");
	err_code = sd_ble_gap_adv_start(advHandle, APP_BLE_CONN_CFG_TAG_PERIPHERAL);
	APP_ERROR_RESET(err_code);
}

void advertisingStop(void) {
	// Stop advertising
	ret_code_t err_code = sd_ble_gap_adv_stop(advHandle);
	APP_ERROR_CONTINUE(err_code);
	NRF_LOG_INFO("Stop Advertising");
}

void disconnectFromDevice(uint16_t connectionHandle) {
	// Start disconnect process.
	if (connectionHandle != BLE_CONN_HANDLE_INVALID) {
		NRF_LOG_INFO("Disconnecting device");
		uint32_t err_code = sd_ble_gap_disconnect(connectionHandle, BLE_HCI_REMOTE_USER_TERMINATED_CONNECTION);
		APP_ERROR_RESET(err_code);
	}
}

void setDataLength(uint16_t connection, BleDataLength_t dataLength) {
	ret_code_t err_code = nrf_ble_gatt_data_length_set(&gattModule, connection, dataLength);
    APP_ERROR_RESET(err_code);
}

uint8_t getDataLength(uint16_t connection) {
	static uint8_t dataLength = 0;
	ret_code_t err_code = nrf_ble_gatt_data_length_get(&gattModule, connection, &dataLength);
    APP_ERROR_CONTINUE(err_code);
	return dataLength;
}

void setGapName(void) {
	char *name = getDeviceName();
	ret_code_t err_code = sd_ble_gap_device_name_set(&sec_mode, name, strlen(name));
	APP_ERROR_RESET(err_code);
}

void setDeviceAppearance(void) {
	uint16_t appearance = 0;
	switch (device) {
		case Device_CatcherGlove:
			appearance = catcherGloveAppearance;
			break;
		case Device_CatcherWrist:
			appearance = catcherWristAppearance;
			break;
		case Device_FielderGlove:
			appearance = fielderGloveAppearance;
			break;
		default:
			NRF_LOG_ERROR("Appearance ERROR: unknown device value");
			appearance = catcherGloveAppearance;
			break;
	}

	NRF_LOG_INFO("Appearance: %x", appearance);
	ret_code_t err_code = sd_ble_gap_appearance_set(appearance);
	APP_ERROR_RESET(err_code);
}
