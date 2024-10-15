#ifndef ADC_HANDLER_H
#define ADC_HANDLER_H

#include "utility.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <nrfx.h>
#include "nrf_saadc.h"
#include "bleService_Battery.h"
#include "bleServices.h"

// Get size of buffer for scheduler union.
#define ADC_CHANNEL_EMPTY			-1
#define ADC_CHANNEL_BATTERY			0

#define NUM_ADC_CHANNELS_BATTERY 	1

#define MAX_ADC_SAMPLES		NUM_SAMPLES_BATTERY // Must be >= the number of samples needed above for each channel set.

void adcInit(void);
void getADC(uint8_t channel, uint16_t numSamples, uint32_t useconds);
uint16_t averageAdcBuffer(int8_t channel, uint8_t numChannels);
void printAdcBuffer(void);

#endif // ADC_HANDLER_H
