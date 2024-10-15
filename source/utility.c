
#include "utility.h"

#include "app_timer.h"
#include "bleInitialize.h"
#include "commands.h"
#include "popTimer.h"
#include "nrf_log.h"
#include <nrfx.h>
#include <stdarg.h>

// Private Variables
#define DEBUG_MAX_SIZE				RESPONSE_PACKET_PAYLOAD_SIZE
char debug[DEBUG_MAX_SIZE];


// Public Function Definitions
void utilityInit(void) {
	// Get RAM copy of ficr since used in high frequency comms for BLE and DW.
	ficrID = (uint64_t) NRF_FICR->DEVICEID[0] + ((uint64_t) NRF_FICR->DEVICEID[1]<< 32);
}

void printBanner(void) {
	NRF_LOG_INFO("\r\n\r\n");
	NRF_LOG_INFO("******************************");
	NRF_LOG_INFO("PopTimer: %s", getDeviceName());
}

void printVersions(void) {
	NRF_LOG_INFO("Model: %s", getDeviceName());
	NRF_LOG_INFO("FW Version %s", (char *)FIRMWARE_REVISION);
	NRF_LOG_INFO("Flash Format Version %d", VERSION_FLASH);
	NRF_LOG_INFO("Bootloader Version %d", bootloaderVersion);
	NRF_LOG_INFO("HW Version %s", HARWARE_REVISION);
	NRF_LOG_INFO("BLE Address: %s\r\n", localBleAddressString);
}

void NRF_LOG_INFO_ARRAY(uint8_t *array, uint8_t size) {
    for (uint16_t i = 0; i < size; i++) {
        NRF_LOG_RAW_INFO("0x%02X ", array[i]);
    }
    NRF_LOG_RAW_INFO("\r\n");
}

void errorHandler(uint32_t err_code, ErrorMode_t mode, uint32_t line_num, const uint8_t *p_file_name){
  	static bool inErrorHandler = false;
	static uint8_t errorCount = 0;
	bool loop = true;

	if (err_code == 0){
		return;
	}

	if (errorCount > MAX_ERROR_COUNT){
	  	NRF_LOG_ERROR("System error %d (%s) occured at %s:%u. Total system errors recorded = %d and exceeds limit. Resetting system...\r\n",err_code,nrf_strerror_get(err_code),p_file_name,line_num,errorCount);
		nrf_delay_ms(500);
		NVIC_SystemReset();
	}

	if (inErrorHandler){
		NRF_LOG_ERROR("FATAL error in error handling %d (%s) occured at %s:%u. Halting execution...\r\n",err_code,nrf_strerror_get(err_code),p_file_name,line_num);
		__disable_irq();
    		while (loop);
		__enable_irq();
	} else {
		inErrorHandler = true;
		switch (mode){
			case CONTINUE:
				errorCount++;
				NRF_LOG_ERROR("System error %d (%s) occured at %s:%u. Total system errors recorded = %d. Continuing execution...\r\n",err_code,nrf_strerror_get(err_code),p_file_name,line_num,errorCount);
				break;

			case RESET_:
				NRF_LOG_ERROR("System error %d (%s) occured at %s:%u. Resetting system...\r\n",err_code,nrf_strerror_get(err_code),p_file_name,line_num);
				NRF_LOG_FINAL_FLUSH();
				nrf_delay_ms(500);
				NRF_LOG_FINAL_FLUSH();
				NVIC_SystemReset();
				break;

			case STOP:
				NRF_LOG_ERROR("System error %d (%s) occured at %s:%u. Halting execution...\r\n",err_code,nrf_strerror_get(err_code),p_file_name,line_num);
				NRF_LOG_FINAL_FLUSH();
				__disable_irq();
				while (loop);
				__enable_irq();
				break;
		}
	}
  	inErrorHandler = false;
}

void debugPrintToBle(char const* format, ...) {
	if (phoneDevice.isConnected) {
		va_list arg;
		va_start(arg, format);
		vsnprintf(debug, DEBUG_MAX_SIZE, format, arg);
		va_end(arg);

		uint16_t length = strnlen(debug, DEBUG_MAX_SIZE);
		sendDataResponsePacket(Cmd_DebugInfo, debug, length);
	}
}

bool isSubset(uint8_t array[], uint8_t subset[], uint8_t arraySize, uint8_t subSize) {
	for (uint8_t i = 0; i < arraySize; i++) {
		if ((i + subSize) > arraySize) {
			return false;
		}

		if (memcmp(&array[i], subset, subSize) == 0) {
			return true;
		}
	}

	return false;
}
