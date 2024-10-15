
#ifndef CENTRAL_HANDLER_H
#define CENTRAL_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "ble.h"
#include <nrfx.h>

#define SEC_PARAM_BOND 0	// Disable bonding.
#define SEC_PARAM_MITM 0 	// Man In The Middle protection not required.
#define SEC_PARAM_LESC 0	// LE Secure Connections not enabled.
#define SEC_PARAM_KEYPRESS 0	// Keypress notifications not enabled.
#define SEC_PARAM_IO_CAPABILITIES BLE_GAP_IO_CAPS_NONE	// No I/O capabilities.
#define SEC_PARAM_OOB 0	// Out Of Band data not available.
#define SEC_PARAM_MIN_KEY_SIZE 7	// Minimum encryption key size in octets.
#define SEC_PARAM_MAX_KEY_SIZE 16	// Maximum encryption key size in octets.

extern ble_gap_scan_params_t scanParams;

void centralInit(void);
void centralDidEvent(void* p_event_data, uint16_t event_size);
void centralStartScan(void);
void centralStopScan(void);
void centralRegisterForNotificationChange(uint16_t cccdHandle, uint16_t connectionHandle);
ret_code_t centralConnect(ble_gap_addr_t *gapAddr);
void setScanFilter(void);

#endif // CENTRAL_HANDLER_H
