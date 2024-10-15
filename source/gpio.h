
#ifndef GPIO_H
#define GPIO_H

#include "nrf_gpio.h"
#include <stdint.h>
#include <stdbool.h>

// Public Function Declarations
void gpioInit(void);
void pinSet(uint32_t pin);
void pinClear(uint32_t pin);


// nRF52 Pinout
#define PIN_XTAL1				0
#define PIN_XTAL2				1
#define PIN_DWM_RST				2
#define PIN_DWM_WAKE			3
#define PIN_TP4					4
#define PIN_VBATT_AN			5
#define PIN_VBATT_EN			6
#define PIN_TP9					7
#define PIN_TP10_Test			8
#define PIN_TP11				9
#define PIN_NC10				10
#define PIN_DWM_INT				11
#define PIN_DWM_SPI_CLK			12
#define PIN_DWM_SPI_MISO		13
#define PIN_ACCEL_SPI_MOSI		14
#define PIN_ACCEL_SPI_MISO		15
#define PIN_ACCEL_CS			16
#define PIN_DWM_SPI_MOSI		17
#define PIN_ACCEL_SPI_CLK		18
#define PIN_DWM_SPI_CS			19
#define PIN_ACCEL_INT1			20
#define PIN_RESET				21
#define PIN_NC22				22
#define PIN_NC23				23
#define PIN_NC24				24
#define PIN_TP37				25
#define PIN_TP38				26
#define PIN_TP39				27
#define PIN_TP40				28
#define PIN_LED_GREEN			29
#define PIN_TP42				30
#define FUNC_SEL				31

#endif // GPIO_H
