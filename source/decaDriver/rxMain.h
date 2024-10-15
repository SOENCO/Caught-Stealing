
#ifndef RX_MAIN_H
#define RX_MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>
#include "dwCommon.h"


// Public Variables


// Public Function Declarations
void rxInit(void);
void rxDeInit(void);
bool processPollMsg(PacketPoll_t *packet);
bool processFinalMsg(PacketFinal_t *packet);
float getLastDistance(void);
void clearDistance(void);

#endif
