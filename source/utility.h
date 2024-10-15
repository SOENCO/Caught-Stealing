
#ifndef UTILITY_H
#define UTILITY_H

#include "ble_gap.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "nrf_log_ctrl.h"
#include "version.h"
#include <stdint.h>
#include <stdbool.h>


#define APP_ERROR_STOP(error) 		errorHandler(error,STOP,__LINE__,(uint8_t*) __FILE__)
#define APP_ERROR_CONTINUE(error) 	errorHandler(error,CONTINUE,__LINE__,(uint8_t*) __FILE__)
#define APP_ERROR_RESET(error)		errorHandler(error,RESET_,__LINE__,(uint8_t*) __FILE__)

#define MAX_ERROR_COUNT				20

#define DEBUG_BLE_PRINT_ENABLED		false
#if DEBUG_BLE_PRINT_ENABLED
	#define debugPrint(...)		do { debugPrintToBle(__VA_ARGS__); NRF_LOG_INFO(__VA_ARGS__); } while(0);
#else
	#define debugPrint(...)		do { NRF_LOG_INFO(__VA_ARGS__); } while(0)
#endif

typedef enum{
  CONTINUE = 0,
  RESET_,
  STOP
} ErrorMode_t;

// Public Function Declarations
void utilityInit(void);
void printBanner(void);
void printVersions(void);
void NRF_LOG_INFO_ARRAY(uint8_t *array, uint8_t size);
void errorHandler(uint32_t err_code, ErrorMode_t mode, uint32_t line_num, const uint8_t * p_file_name);
void debugPrintToBle(char const* format, ...);
bool isSubset(uint8_t array[], uint8_t subset[], uint8_t arraySize, uint8_t subSize);

#endif
