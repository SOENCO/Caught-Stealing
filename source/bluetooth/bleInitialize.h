
#ifndef BLE_INITIALIZE_H
#define BLE_INITIALIZE_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <nrfx.h>
#include "ble_gap.h"
#include "utility.h"


#define APP_BLE_CONN_CFG_TAG_PERIPHERAL  	1	// A tag identifying the SoftDevice BLE configuration.
#define APP_BLE_CONN_CFG_TAG_CENTRAL  		2	// A tag identifying the SoftDevice BLE configuration.

#define CONNECTABLE_ADV_INTERVAL_20_MS        MSEC_TO_UNITS(20, UNIT_0_625_MS)	// Advertising interval for connectable advertisement (20 ms). This value can vary between 20ms to 10.24s.
#define CONNECTABLE_ADV_INTERVAL_187_MS        MSEC_TO_UNITS(187, UNIT_0_625_MS)	// Advertising interval for connectable advertisement (187 ms). This value can vary between 20ms to 10.24s.
#define NON_CONNECTABLE_ADV_INTERVAL    MSEC_TO_UNITS(100, UNIT_0_625_MS)	// Advertising interval for non-connectable advertisement (100 ms). This value can vary between 100ms to 10.24s).

#define MIN_CONNECTION_INTERVAL (uint16_t) MSEC_TO_UNITS(7.5, UNIT_1_25_MS) // Minimum connection interval in milliseconds.
//#define MIN_CONNECTION_INTERVAL (uint16_t) MSEC_TO_UNITS(200, UNIT_1_25_MS) // Minimum connection interval in milliseconds.
#define MAX_CONNECTION_INTERVAL (uint16_t) MSEC_TO_UNITS(500, UNIT_1_25_MS) // Maximum connection interval in milliseconds.
#define SLAVE_LATENCY 0 // Slave latency in terms of connection events.
#define SUPERVISION_TIMEOUT (uint16_t) MSEC_TO_UNITS(4000, UNIT_10_MS) // Supervision time-out in units of 10 milliseconds.

#define APP_BLE_OBSERVER_PRIO	2 // Priority of BLE event handler. DO NOT USE 3 or HIGHER OR IT WILL NOT SEE DISCOVER ADVERTISING PACKETS OF DEVICES AROUND (while in Central role). http://infocenter.nordicsemi.com/index.jsp?topic=%2Fcom.nordic.infocenter.s132.sds%2Fdita%2Fsoftdevices%2Fs130%2Fprocessor_avail_interrupt_latency%2Fexception_mgmt_sd.html

#define TX_POWER_LEVEL 4

#define FIRST_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(5000) // Time from initiating an event (connect or start of notification) to the first time sd_ble_gap_conn_param_update is called (5 seconds).
#define NEXT_CONN_PARAMS_UPDATE_DELAY APP_TIMER_TICKS(30000) // Time between each call to sd_ble_gap_conn_param_update after the first call (30 seconds).
#define MAX_CONN_PARAMS_UPDATE_COUNT 3 // Max count of connection parameter update.


typedef PACKED_STRUCT {
	bool isConnected;
	bool hasPasskey;
	uint16_t connectionHandle;
	uint8_t addr[BLE_GAP_ADDR_LEN];
	bool needsNotified_data;
} PhoneDevice_t;

typedef enum {
	DataLength_Peripheral = 251,
	DataLength_Central = 251
} BleDataLength_t;

extern ble_gap_conn_params_t connectionParameters;
extern ble_gap_addr_t localBleAddress;
extern char localBleAddressString[];
extern uint16_t mtuPayloadSize;
extern PhoneDevice_t phoneDevice;
extern bool isAuthed;

void bleStackInit(void);
void bleInit(void);
void advertisingStart(bool isFastInterval);
void advertisingStop(void);
void disconnectFromDevice(uint16_t connectionHandle);
void setDataLength(uint16_t connection, BleDataLength_t dataLength);
uint8_t getDataLength(uint16_t connection);
void setGapName(void);
void setDeviceAppearance(void);

#endif // BLE_INITIALIZE_H
