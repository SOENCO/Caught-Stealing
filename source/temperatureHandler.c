
#include "temperatureHandler.h"

#include "adcHandler.h"
#include "app_scheduler.h"
#include "app_timer.h"
#include "nrf_log.h"
#include "nrf_temp.h"
#include "utility.h"
#include "gpio.h"

// Module Init
APP_TIMER_DEF(temperatureDelay_id);

// Public data

// Private data
static int8_t temperature = 0;

// Private Declarations
static void temperatureDelayTimer_start(void);
static void temperatureDelayTimer_handler(void * context);
static void temperatureDelayTimer_handlerAppContext(void *p_event_data, uint16_t event_size);

// Private Function Definitions
static void temperatureDelayTimer_start(void) {
	app_timer_stop(temperatureDelay_id);
	uint32_t err_code = app_timer_start(temperatureDelay_id, APP_TIMER_TICKS(TEMPERATURE_FREQ_MS), NULL);
	APP_ERROR_CONTINUE(err_code);
}

static void temperatureDelayTimer_handler(void *context) {
	// Move to App Context
	ret_code_t err_code = app_sched_event_put(NULL, 0, temperatureDelayTimer_handlerAppContext);
	APP_ERROR_CONTINUE(err_code);
}

static void temperatureDelayTimer_handlerAppContext(void *p_event_data, uint16_t event_size) {
	int32_t tempX4 = 0;
	sd_temp_get(&tempX4);
	temperature = (int8_t) (tempX4 / 4);
	// NRF_LOG_INFO("Temperature %d", temperature);
}

// Public Function Definitions
void temperatureInit(void) {
	// Init timer
	uint32_t err_code = app_timer_create(&temperatureDelay_id, APP_TIMER_MODE_REPEATED, temperatureDelayTimer_handler);
	APP_ERROR_CONTINUE(err_code);

	temperatureDelayTimer_handler(NULL);	// Grab value at startup
	temperatureDelayTimer_start();
}

int8_t getTemperature(void) {
	return temperature;
}
