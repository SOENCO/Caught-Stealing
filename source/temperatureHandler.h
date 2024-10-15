
#ifndef TEMPERATURE_HANDLER_H
#define TEMPERATURE_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <nrfx.h>


#define TEMPERATURE_FREQ_MS					8000
#define TEMPERATURE_POLL_READY_FREQ_MS		5
#define TEMPERATURE_UNKNOWN					255

void temperatureInit(void);
int8_t getTemperature(void);

#endif
