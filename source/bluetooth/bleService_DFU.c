
#include "bleService_DFU.h"

#include "app_timer.h"
#include "ble_dfu.h"
#include "nrf_pwr_mgmt.h"
#include "nrf_log.h"
#include "nrf_sdm.h"
#include "utility.h"

// Private Function Declarations
static void dfuEventHandler(ble_dfu_buttonless_evt_type_t event);
static bool appShutDownHandler(nrf_pwr_mgmt_evt_t event);

// Module Init
NRF_PWR_MGMT_HANDLER_REGISTER(appShutDownHandler, 0);

void service_DfuInit(void) {
	uint32_t err_code;
	ble_dfu_buttonless_init_t dfus_init = {
	    .evt_handler = dfuEventHandler};

	// See if bootloader exists
	if (NRF_UICR->NRFFW[0] == 0xFFFFFFFF) {
		NRF_LOG_INFO("No bootloader = No DFU Service");
		return;
	}

	// Initialize the async SVCI interface to bootloader.
	err_code = ble_dfu_buttonless_async_svci_init();
	APP_ERROR_RESET(err_code);

	err_code = ble_dfu_buttonless_init(&dfus_init);
	APP_ERROR_RESET(err_code);
}

static bool appShutDownHandler(nrf_pwr_mgmt_evt_t event) {
	// During shutdown procedures, this function will be called at a 1 second interval until the function returns true. When the function returns true, it means that the app is ready to reset to DFU mode.
	// Returns True if shutdown is allowed by this power manager handler, otherwise false.

	switch (event) {
		case NRF_PWR_MGMT_EVT_PREPARE_DFU:
			NRF_LOG_INFO("Power management wants to reset to DFU mode.");
			// TODO: Get ready to reset into DFU mode
			// If you aren't finished with any ongoing tasks, return "false" to signal to the system that reset is impossible at this stage.

			// Example using a variable to delay resetting the device.
			// if (!m_ready_for_reset) {
			//	  return false;
			// }

			// Device ready to enter
			uint32_t err_code;
			err_code = sd_softdevice_disable();
			APP_ERROR_RESET(err_code);
			err_code = app_timer_stop_all();
			APP_ERROR_RESET(err_code);
			break;

		default:
			// TODO: Implement any of the other events available from the power management module:
			//	  -NRF_PWR_MGMT_EVT_PREPARE_SYSOFF
			//	  -NRF_PWR_MGMT_EVT_PREPARE_WAKEUP
			//	  -NRF_PWR_MGMT_EVT_PREPARE_RESET
			return true;
	}

	NRF_LOG_INFO("Power management allowed to reset to DFU mode.");
	return true;
}

static void dfuEventHandler(ble_dfu_buttonless_evt_type_t event) {
	// Handles dfu events from the Buttonless Secure DFU service
	switch (event) {
		case BLE_DFU_EVT_BOOTLOADER_ENTER_PREPARE:
			NRF_LOG_INFO("Device is preparing to enter bootloader mode.");
			// Disconnect all bonded devices that currently are connected. This is required to receive a service changed indication on bootup after a successful (or aborted) Device Firmware Update.
			break;

		case BLE_DFU_EVT_BOOTLOADER_ENTER:
			NRF_LOG_INFO("Device is requested to enter bootloader mode!");
			// TODO: shut down external chips
			break;

		case BLE_DFU_EVT_BOOTLOADER_ENTER_FAILED:
			NRF_LOG_INFO("Request to enter bootloader mode failed asynchroneously.");
			// TODO: Take corrective measures to resolve the issue like calling APP_ERROR_RESET to reset the device.
			break;

		case BLE_DFU_EVT_RESPONSE_SEND_ERROR:
			NRF_LOG_INFO("Request to send a response to client failed.");
			// TODO: Take corrective measures to resolve the issue like calling APP_ERROR_RESET to reset the device.
			APP_ERROR_RESET(false);
			break;

		default:
			NRF_LOG_INFO("Unknown event from ble_dfu_buttonless.");
			break;
	}
}
