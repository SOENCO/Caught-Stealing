
#include "system.h"
#include "app_error.h"
#include "app_timer.h"
#include "dwCommon.h"
#include "gpio.h"
#include "LIS2DW12.h"
#include "popTimer.h"
#include "nrf_gpio.h"
#include "nrf_log_ctrl.h"
#include "nrf_log_default_backends.h"
#include "sdk_errors.h"
#include <string.h>
#include "utility.h"
#include "nrfx_rtc.h"

const nrfx_rtc_t rtc = NRFX_RTC_INSTANCE(2);

// Private Variables
static volatile uint16_t rtcOverflow = 0;
static SleepSelect_t sleepSelect = Sleep_WakeAll;


// Private Function Declarations
static void rtcHandler(nrfx_rtc_int_type_t int_type);


// Private Function Definitions
static void rtcHandler(nrfx_rtc_int_type_t int_type) {
    if (int_type == NRFX_RTC_INT_OVERFLOW) {
        rtcOverflow++;
    }
}

// Public Function Definitions
void logInit(void) {
	ret_code_t err_code = NRF_LOG_INIT(NULL);
	APP_ERROR_CONTINUE(err_code);

	NRF_LOG_DEFAULT_BACKENDS_INIT();
}

void powerManage(void) {
	ret_code_t err_code = sd_app_evt_wait();
	APP_ERROR_RESET(err_code);
}

void timerModuleInit(void) {
	// Timer module.
	ret_code_t err_code = app_timer_init();
	APP_ERROR_CONTINUE(err_code);
}

void rtcInit(void) {
    uint32_t err_code;

    // Initialize RTC instance
    nrfx_rtc_config_t config = NRFX_RTC_DEFAULT_CONFIG;
	//config.prescaler = 0;
	// NOTE: Changing the prescaler in config doesn't seem to have an effect, use APP_TIMER_CONFIG_RTC_FREQUENCY instead.
	// Prescaler	Counter Resolution		Oveflow
	// 0			30.517 usec				512 seconds
	// 2^8 - 1		7812.5 usec				131072 seconds
	// 2^12 - 1		125 msec				582.542 hours

    err_code = nrfx_rtc_init(&rtc, &config, rtcHandler);
    APP_ERROR_CHECK(err_code);

    //Enable tick event & interrupt
    nrfx_rtc_tick_enable(&rtc, true);

    //Set compare channel to trigger interrupt after COMPARE_COUNTERTIME seconds
    // err_code = nrfx_rtc_cc_set(&rtc, 0, COMPARE_COUNTERTIME * 8, true);
    // APP_ERROR_CHECK(err_code);

    //Power on RTC instance
    nrfx_rtc_enable(&rtc);
}

Timestamp_t getNow(void) {
	// Returns 'now' as a 40-bit timestamp. nRF's RTC COUNTER is 24-bit. By using a 16-bit overflow counter, we get the desired 40-bit,
	uint16_t overflow = 0;
	uint32_t now24Bit = 0;

	// Ensure atomic read of overflow variable.
	do {
		overflow = rtcOverflow;
		now24Bit = app_timer_cnt_get();
	} while (overflow != rtcOverflow);

	Timestamp_t timestamp;
	timestamp.ticks = ((uint64_t) overflow << 24) + now24Bit;
	return timestamp;
}

void wakeInit(void) {
	// Configures nRF's wake from deep sleep.
	nrf_gpio_pin_sense_t sense = NRF_GPIO_PIN_SENSE_HIGH;
	nrf_gpio_cfg_sense_set(PIN_ACCEL_INT1, sense);
}

void systemSleep(SleepSelect_t select) {
	// Wakes or sleeps internal and/or external devices.

	switch (select) {
		case Sleep_WakeAll:
			// nRF auto-wakes based on wakeInit() setting, ie PIN_ACCEL_INT1.
			debugPrint("Sleep_WakeAll");
			accelSetSleep(false);
			dwSetSleep(false);
			break;
		case Sleep_ExtPeripherals:
			debugPrint("Sleep_ExtPeripherals");
			sessionTimeout_stop();
			dwSetSleep(true);
			accelSetSleep(true);	// Note: Even though put to sleep here, any movement will cause it to wake again.
			// nRF leave awake.
			break;
		case Sleep_All:
			debugPrint("Sleep_All");
			sessionTimeout_stop();
			dwSetSleep(true);
			accelSetSleep(true);	// Note: Even though put to sleep here, any movement will cause it to wake again.
			sd_power_system_off();	// Note that when nRF is put to sleep with sd_power_system_off(), then nRF chip resets from beginning when awoken and firmware reboots.
			break;
	}

	// Set sleep flag AFTER all peripherals have had time to sleep/wake.
	sleepSelect = select;
}

bool isDwSleeping(void) {
	// Don't bother checking for Sleep_All. Softdevice isn't running when Sleep_All = true.
	return (sleepSelect == Sleep_ExtPeripherals);
}

void enableCodeProtect(void) {
	if (NRF_UICR->APPROTECT != 0x0) {
		NRF_NVMC->CONFIG = 0x1;
		NRF_UICR->APPROTECT = 0x0;
		NRF_NVMC->CONFIG = 0x0;
	}
}