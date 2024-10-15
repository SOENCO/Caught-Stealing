
#include "bleService_Battery.h"

#include "adcHandler.h"
#include "app_scheduler.h"
#include "app_timer.h"
#include "ble_bas.h"
#include "nrf_log.h"
#include "utility.h"
#include "gpio.h"

// Module Init
BLE_BAS_DEF(batteryService);
APP_TIMER_DEF(batterTimer_id);

// Public data

// Private data
#define BATT_FULL_ADC	545		// 4.1v @ V_BATT
#define BATT_EMPTY_ADC	439		// 3.3v @ V_BATT
#define BATT_FULL_VOLT	4.1
#define BATT_EMPTY_VOLT	3.3
#define BATT_FREQ_MS	(60 * 5 * 1000)	// 5 minutes

static uint8_t batteryPercent = 0;

// Private Declarations
static void batteryUpdateGATT(uint8_t batteryLevel);
static void batteryEventHandler(ble_bas_t *p_bas, ble_bas_evt_t *p_evt);
static void batteryTimer_start(void);
static void batteryTimer_handler(void * context);
static void batteryTimer_handlerAppContext(void *p_event_data, uint16_t event_size);

// Private Function Definitions
static void batteryUpdateGATT(uint8_t batteryLevel) {
	ret_code_t err_code = ble_bas_battery_level_update(&batteryService, batteryLevel, BLE_CONN_HANDLE_ALL);
	// DO NOT check for error here. Since we aren't monitoring if battery service is ready to be notified or not during a connection, just ignore the error here.
}

static void batteryEventHandler(ble_bas_t *p_bas, ble_bas_evt_t *p_evt) {
	NRF_LOG_INFO("%s: Notify %s", __FUNCTION__ , (p_evt->evt_type == BLE_BAS_EVT_NOTIFICATION_ENABLED) ? "Enabled" : "Disabled");

	// Grab battery value after BLE connection and notify registered
	if (p_evt->evt_type == BLE_BAS_EVT_NOTIFICATION_ENABLED) {
		batteryTimer_handler(NULL);
	}
}

static void batteryTimer_start(void) {
	app_timer_stop(batterTimer_id);
	uint32_t err_code = app_timer_start(batterTimer_id, APP_TIMER_TICKS(BATT_FREQ_MS), NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void batteryTimer_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, batteryTimer_handlerAppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void batteryTimer_handlerAppContext(void *p_event_data, uint16_t event_size) {
	pinSet(PIN_VBATT_EN);
	getADC(ADC_CHANNEL_BATTERY, NUM_SAMPLES_BATTERY, ADC_FREQ_BATTERY_USEC);
}

// Public Function Definitions
void service_BatteryInit(void) {
	// Initialize the Battery Service.
	ble_bas_init_t bas_init_struct;
	ret_code_t err_code;

	memset(&bas_init_struct, 0, sizeof(bas_init_struct));

	bas_init_struct.evt_handler = batteryEventHandler;
	bas_init_struct.support_notification = true;
	bas_init_struct.p_report_ref = NULL;
	bas_init_struct.initial_batt_level = batteryPercent;

	// Require LESC with MITM (Numeric Comparison)
	bas_init_struct.bl_rd_sec = SEC_OPEN;
	bas_init_struct.bl_cccd_wr_sec = SEC_OPEN;
	bas_init_struct.bl_report_rd_sec = SEC_OPEN;

	err_code = ble_bas_init(&batteryService, &bas_init_struct);
	APP_ERROR_CONTINUE(err_code);
}

void batteryTimerInit(void) {
	// Init timer
	uint32_t err_code = app_timer_create(&batterTimer_id, APP_TIMER_MODE_REPEATED, batteryTimer_handler);
	APP_ERROR_CONTINUE(err_code);

	batteryTimer_handler(NULL);	// Grab battery value at startup
	batteryTimer_start();
}

void battery_callBack(void *p_event_data, uint16_t event_size) {
	int16_t averageAdc = averageAdcBuffer(ADC_CHANNEL_BATTERY, NUM_ADC_CHANNELS_BATTERY);
	float voltage = 0.0;

	if (averageAdc <= BATT_EMPTY_ADC) {
		batteryPercent = 0;
		voltage = BATT_EMPTY_VOLT;
	} else if (averageAdc >= BATT_FULL_ADC) {
		batteryPercent = 100;
		voltage = BATT_FULL_VOLT;
	} else {
		batteryPercent = (((averageAdc - BATT_EMPTY_ADC) * 100) / (BATT_FULL_ADC - BATT_EMPTY_ADC));
		voltage = (BATT_FULL_VOLT * averageAdc) / BATT_FULL_ADC;
	}

	batteryUpdateGATT(batteryPercent);
	NRF_LOG_INFO("Battery %% = %d, Adc = %d, Volt = " NRF_LOG_FLOAT_MARKER "\r\n", batteryPercent, averageAdc, NRF_LOG_FLOAT(voltage));
	pinClear(PIN_VBATT_EN);
}

uint8_t getBatteryLevel(void) {
	return batteryPercent;
}
