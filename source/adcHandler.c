
#include "adcHandler.h"

#include "app_error.h"
#include "app_scheduler.h"
#include "nrf.h"
#include "nrf_delay.h"
#include "nrfx_ppi.h"
#include "nrf_drv_ppi.h"
#include "nrfx_timer.h"
#include "nrfx_saadc.h"
#include "nrf_log.h"
#include "nrfx_saadc.h"
#include <limits.h>

// Public data

// Private data
static nrf_saadc_value_t adcBuffer[MAX_ADC_SAMPLES];
static uint16_t lastAdcNumSamples = MAX_ADC_SAMPLES;
static bool isAdcRunning = false;
// TIMER0 is used by the softdevice, use instance 1
static const nrfx_timer_t ppiTimer = NRFX_TIMER_INSTANCE(1);
static nrf_ppi_channel_t ppiChannel;

int8_t activeChannel = ADC_CHANNEL_EMPTY;

// Private Declarations
static void saadcInit(void);
static void ppiInit(void);
static void setTimerFrequency(uint32_t useconds);
static void setActiveChannel(uint8_t channel);
static void saadc_callback(nrfx_saadc_evt_t const *p_event);
static void ppiTimerHandler(nrf_timer_event_t event_type, void *p_context);

// Private Definitions
static void saadcInit(void) {
	// Initialize saadc
	ret_code_t err_code;

	nrfx_saadc_config_t saadc_config = NRFX_SAADC_DEFAULT_CONFIG;
	err_code = nrfx_saadc_init(&saadc_config, saadc_callback);
	APP_ERROR_CONTINUE(err_code);
}

static void ppiInit(void) {
	// Use ppi for handling ADC sample frequency.
    ret_code_t err_code;

    nrfx_timer_config_t timer_cfg = NRFX_TIMER_DEFAULT_CONFIG;
    timer_cfg.bit_width = NRF_TIMER_BIT_WIDTH_32;
    err_code = nrfx_timer_init(&ppiTimer, &timer_cfg, ppiTimerHandler);
    APP_ERROR_CONTINUE(err_code);

    // Setup ppiTimer for compare event every X usec.
    setTimerFrequency(ADC_FREQ_BATTERY_USEC);

    uint32_t timer_compare_event_addr = nrfx_timer_compare_event_address_get(&ppiTimer, NRF_TIMER_CC_CHANNEL0);
    uint32_t saadc_sample_task_addr = nrfx_saadc_sample_task_get();

    err_code = nrf_drv_ppi_init();
    APP_ERROR_CONTINUE(err_code);

    // Setup ppi channel so that timer compare event is triggering sample task in SAADC.
    err_code = nrfx_ppi_channel_alloc(&ppiChannel);
    APP_ERROR_CONTINUE(err_code);

    err_code = nrfx_ppi_channel_assign(ppiChannel, timer_compare_event_addr, saadc_sample_task_addr);
    APP_ERROR_CONTINUE(err_code);
}

static void setTimerFrequency(uint32_t useconds) {
    nrfx_timer_disable(&ppiTimer);
	uint32_t ticks = nrfx_timer_us_to_ticks(&ppiTimer, useconds);
	nrfx_timer_extended_compare(&ppiTimer, NRF_TIMER_CC_CHANNEL0, ticks, NRF_TIMER_SHORT_COMPARE0_CLEAR_MASK, false);
    nrfx_timer_enable(&ppiTimer);
}

static void setActiveChannel(uint8_t channel) {
	// Sets active channel, inits/uninits.
	ret_code_t err_code;

	if (activeChannel == channel) {
		return;
	}
	activeChannel = channel;

	// Un-init all channels
	err_code = nrfx_saadc_channel_uninit(ADC_CHANNEL_BATTERY);
	APP_ERROR_CONTINUE(err_code);

	switch (activeChannel) {
		case ADC_CHANNEL_BATTERY:
		{
			// AIN3/P0.05: V_BATT_AN
			nrf_saadc_channel_config_t configChannelA3 = NRFX_SAADC_DEFAULT_CHANNEL_CONFIG_SE(NRF_SAADC_INPUT_AIN3);
			err_code = nrfx_saadc_channel_init(ADC_CHANNEL_BATTERY, &configChannelA3);
			APP_ERROR_CONTINUE(err_code);
		}
		break;

		default:
		break;
	}
}

static void saadc_callback(nrfx_saadc_evt_t const *p_event) {
	// This is called on several events:
	// - When buffer is filled with Samples, per call to nrfx_saadc_buffer_convert and each callback contains samples from every enabled ADC channel.
	// - When a limit reached.
	// - After nrfx_saadc_calibrate_offset completes.
	ret_code_t err_code;

	switch (p_event->type) {
		case NRFX_SAADC_EVT_DONE:
			// Callback for nrfx_saadc_buffer_convert and contains samples from every enabled channel.

			// Stop ADC sampling
			err_code = nrfx_ppi_channel_disable(ppiChannel);
			APP_ERROR_CONTINUE(err_code);

			// Send data
			switch (activeChannel) {
				case ADC_CHANNEL_BATTERY:
					app_sched_event_put(NULL, 0, battery_callBack);
				break;

				default:
				break;
			}

			isAdcRunning = false;
		break;
		case NRFX_SAADC_EVT_LIMIT:
			NRF_LOG_ERROR("NRFX_SAADC_EVT_LIMIT");
		break;
		case NRFX_SAADC_EVT_CALIBRATEDONE:
			NRF_LOG_INFO("NRFX_SAADC_EVT_CALIBRATEDONE");
		break;
	}
}

static void ppiTimerHandler(nrf_timer_event_t event_type, void *p_context) {
}

// Public Definitions

void adcInit(void) {
	// Initialize ADC channels
	saadcInit();
	ppiInit();
}

void getADC(uint8_t channel, uint16_t numSamples, uint32_t useconds) {
	// This function starts the asynchronous process of grabbing ADC values. Once the final ADC conversion is completed, callBack is called with the sampled values.
    ret_code_t err_code;

	if (isAdcRunning) {
		return;
	}
	isAdcRunning = true;

	// Set active channel so we know which callback to use.
	setActiveChannel(channel);
	lastAdcNumSamples = numSamples;

	if (lastAdcNumSamples > MAX_ADC_SAMPLES) {
		lastAdcNumSamples = MAX_ADC_SAMPLES;
	}

	// Set timer freq
	setTimerFrequency(useconds);

	// nrfx_saadc_buffer_convert is non-blocking and Samples are not put in buffer until NRFX_SAADC_EVT_DONE callback in saadc_callback(). Calling it below lets the SDK set it up to be ready when Sampling is completed. Conversion will be done on all enabled channels. The SDK only allows max of 2 data sets at a time (ie 2 calls to function before first completes). Technically it can be the same ADC pin for both sets, but for this application using 1 buffer for the 3 ADC pins (AIN3 & AIN6 & AIN7)
    err_code = nrfx_saadc_buffer_convert(adcBuffer, lastAdcNumSamples);
    APP_ERROR_CONTINUE(err_code);

	// Enable ppi to start triggering ADC samples.
    err_code = nrfx_ppi_channel_enable(ppiChannel);
    APP_ERROR_CONTINUE(err_code);
}

uint16_t averageAdcBuffer(int8_t channel, uint8_t numChannels) {
	double arrayMin, arrayMax;
	double arraySum, arrayAverage;
	uint16_t *bptr;
	uint16_t i;

	if (lastAdcNumSamples <= numChannels) {
		NRF_LOG_ERROR("average data: Error size=%d", lastAdcNumSamples);
		return 0;
	}

	// Find Sum/Max/Min points
	uint32_t sum = 0;
	uint16_t numSamples = 0;
	nrf_saadc_value_t min = SHRT_MAX;
	nrf_saadc_value_t max = 0;

	// Set starting index based on channel
	for (int16_t index = channel; index < lastAdcNumSamples; index += numChannels) {
		nrf_saadc_value_t value = adcBuffer[index];
		if (value < 0) {
			value = 0;
		}

		sum += value;
		numSamples++;
		if (min > value) {
			min = value;
		}
		if (max < value) {
			max = value;
		}
	}

	// Remove Max/Min points
	sum -= min;
	sum -= max;

	// Average
	uint32_t average = sum / (numSamples - 2);
	return average;
}

void printAdcBuffer(void) {
	NRF_LOG_INFO("adcBuffer:");
	NRF_LOG_INFO_ARRAY((uint8_t*) adcBuffer, lastAdcNumSamples);
}
