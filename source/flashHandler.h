
#ifndef FLASH_HANDLER_H
#define FLASH_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <nrfx.h>

#include "popTimer.h"
#include "commands.h"

// Size must be divisible by 4 (int32)
typedef PACKED_STRUCT {
	uint8_t flashVersion;
	uint8_t flashSize;
	char fullname[DEVICE_NAME_LENGTH_PLUS_NULL];
	uint16_t sessionTimeout;
	uint16_t groupId;
	uint64_t catcherGloveFICR;
	uint64_t catcherWristFICR;
	uint64_t fielderGloveFICR;
	uint32_t ownerId;
	//uint8_t emptyFlashSpacers[1];
} Flash_t;
extern Flash_t storage;

void flashInit(void);
void updateFlash(void);

#endif // FLASH_HANDLER_H
