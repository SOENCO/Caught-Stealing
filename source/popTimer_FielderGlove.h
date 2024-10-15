
#ifndef POP_TIMER_FIELDER_GLOVE_H
#define POP_TIMER_FIELDER_GLOVE_H

#include <stdint.h>
#include <stdbool.h>
#include <nrfx.h>
#include "popTimer.h"


// Public Variables


// Public Function Declarations
void popTimerLoop_FielderGlove(void);
SessionInfo_t getSessionInfo_FielderGlove(void);
uint32_t getSessionID_FielderGlove(void);
void setSessionID_FielderGlove(uint32_t id);

#endif
