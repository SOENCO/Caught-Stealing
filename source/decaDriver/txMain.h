
#ifndef TX_MAIN_H
#define TX_MAIN_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>
#include "dwCommon.h"


// Receive response timeout. See NOTE 5 below.
#define RESP_RX_TIMEOUT_UUS		2700
#define RESP_RX_TO_FINAL_TX_DLY_UUS 3100


// Public Variables


// Public Function Declarations
void txInit(void);
void txDeInit(void);
void processReplyMsg(PacketReply_t *packet);

#endif
