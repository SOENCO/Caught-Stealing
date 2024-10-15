
#ifndef BLE_SERVICE_BATTERY_H
#define BLE_SERVICE_BATTERY_H

#include "utility.h"

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <nrfx.h>

#define ADC_FREQ_BATTERY_USEC		15000
#define NUM_SAMPLES_BATTERY			120

void service_BatteryInit(void);
void batteryTimerInit(void);
void battery_callBack(void *p_event_data, uint16_t event_size);
uint8_t getBatteryLevel(void);

#endif // BLE_SERVICE_BATTERY_H
