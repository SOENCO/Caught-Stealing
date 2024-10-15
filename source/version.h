
#ifndef VERSION_H
#define VERSION_H

#include <stdint.h>

#define MANUFACTURER_NAME		"Caught Stealing"
#define MODEL_NUMBER			"PRO 100"
#define HARWARE_REVISION		"0"		// TODO: This should be set based on hardware inputs, not hard-coded.
#define FIRMWARE_REVISION		"2.01"
#define FIRMWARE_REVISION_INT	(uint32_t) 201

// Integer versions must stay within 1 byte.
#define VERSION_FLASH		6	// Version of flash format

extern uint8_t bootloaderVersion;

void versionInit(void);

#endif	// VERSION_H
