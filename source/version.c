
#include "version.h"
#include "nrf_power.h"


// Public Variables
uint8_t bootloaderVersion = 0;


// Public Function Definitions
void versionInit(void) {
	bootloaderVersion = NRF_POWER->GPREGRET2;
}
