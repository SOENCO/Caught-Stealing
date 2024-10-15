
#ifndef UNGROUP_HANDLER_H
#define UNGROUP_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include "commands.h"


// Public Variables


// Public Function Declarations
void ungroupLoop(void);
void unGroupViaBLE(CommandPacket_Ungroup_t *packet);
void unGroup(void);

#endif
