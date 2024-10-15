
#include "watchDogHandler.h"
#include "nrfx_wdt.h"
#include "utility.h"

// Module Init

// Public variables

// Private variables
nrfx_wdt_channel_id wdtChannel;

// Private Function Declarations
static void wdtEventHandler(void);

// Private Function Definitions
static void wdtEventHandler(void) {
    //NOTE: The max amount of time we can spend in WDT interrupt is two cycles of 32768[Hz] clock - after that, reset occurs
}

// Public Function Definitions
void watchDogInit(void) {
    nrfx_wdt_config_t config = NRFX_WDT_DEAFULT_CONFIG;

	nrfx_err_t err_code;
	err_code = nrfx_wdt_init(&config, wdtEventHandler);
    APP_ERROR_CONTINUE(err_code);

    err_code = nrfx_wdt_channel_alloc(&wdtChannel);
    APP_ERROR_CONTINUE(err_code);

    nrfx_wdt_enable();
}

void watchDogFeed(void) {
	nrfx_wdt_channel_feed(wdtChannel);
}
