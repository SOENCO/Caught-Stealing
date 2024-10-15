
#ifndef PERIPHERAL_HANDLER_H
#define PERIPHERAL_HANDLER_H

#include "ble_gatts.h"
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>


// Public Function Declarations
void peripheralInit(void);
void peripheralDidEvent(void* p_event_data, uint16_t event_size);
void writeToGatt(ble_gatts_char_handles_t *gattHandle, uint8_t *data, uint16_t length);
void authTimer_stop(void);

#endif // PERIPHERAL_HANDLER_H
