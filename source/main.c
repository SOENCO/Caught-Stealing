
#include "adcHandler.h"
#include "app_scheduler.h"
#include "bleInitialize.h"
#include "bleService_Battery.h"
#include "dwCommon.h"
#include "flashHandler.h"
#include "gpio.h"
#include "LIS2DW12.h"
#include "peripheralHandler.h"
#include "popTimer.h"
#include "scheduler.h"
#include "system.h"
#include "temperatureHandler.h"
#include "ungroupHandler.h"
#include "utility.h"
#include "watchDogHandler.h"
#include "version.h"


// CRITICAL: DecaWave related code is very time sensitive. This also includes sensitivity to code optimization level. Ensure both Debug & Release builds use gcc_optimization_level="Debug". Otherwise the distance will always = 0. If a different optimization level MUST be used for other reasons, then DecaWave code will need refined to work with it.

int main(void) {
	enableCodeProtect();
	logInit();
	timerModuleInit();
	rtcInit();
	gpioInit();
	commandsInit();
	schedulerInit();
	bleStackInit();
	flashInit();	// Before bleInit
	versionInit();	// Before bleInit
	bleInit();
	peripheralInit();
	utilityInit();	// After bleInit so ble address is grabbed

	adcInit();
	batteryTimerInit();
	temperatureInit();
	accelInit();
	wakeInit();
	dwInit();		// Call before popTimerInit
	popTimerInit();
	printVersions();
	watchDogInit();

	systemSleep(Sleep_ExtPeripherals);
	while (true) {
		watchDogFeed();
		readXYZ();
		dwServiceInterrupt();
		popTimerLoop();
		accelLoop();
		ungroupLoop();

		app_sched_execute();
		NRF_LOG_PROCESS();
	}
}
