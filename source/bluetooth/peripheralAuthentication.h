#ifndef PERIPHERAL_AUTHENTICATION_H
#define PERIPHERAL_AUTHENTICATION_H

#include <stdint.h>
#include <stdbool.h>
#include "commands.h"


 //Public Function Prototypes
void peripheralAuth_step1(void);
void peripheralAuth_step2(Auth02_t* auth02);

#endif
