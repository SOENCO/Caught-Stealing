
#ifndef BLE_SERVICES_H
#define BLE_SERVICES_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ble_srv_common.h"
#include "ble_types.h"
#include "nrf_log.h"
#include <nrfx.h>

#define MAX_BLE_TX_SIZE		512	// Note: 512 is maximum can use without hvx error. Negotiated MTU ultimately sets the size used. This var is used for size caps in the code.

#define CHAR_UUID_COMMAND			0x0101
#define CHAR_UUID_DATA				0x0102

#define COMMAND_MAX_SIZE	40
#define DATA_MAX_SIZE		40

extern const uint16_t catcherGloveAppearance;
extern const uint16_t catcherWristAppearance;
extern const uint16_t fielderGloveAppearance;

typedef struct {
	uint16_t service_handle;               // Service handle as provided by the BLE stack.
	uint16_t conn_handle;                  // Handle of the current connection (as provided by the BLE stack, is BLE_CONN_HANDLE_INVALID if not in a connection).
	ble_srv_error_handler_t error_handler; // Function to be called in case of an error.
} ServiceCfg_t;

extern const ble_uuid128_t primaryServiceUUID128;
extern ble_uuid_t primaryServiceUUID;
extern ble_gatts_char_handles_t gattCommandHandle;
extern ble_gatts_char_handles_t gattDataHandle;

void servicesInit(void);
ret_code_t assignConnHandleToQwr(uint16_t connectionHandle);

#endif // BLE_SERVICES_H
