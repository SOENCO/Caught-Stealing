
#include "gpio.h"

#include "app_timer.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "utility.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>


// Public Function Definitions
void gpioInit(void) {
	// Configure Input/Output and Pulls
	nrf_gpio_cfg_default(PIN_XTAL1);
	nrf_gpio_cfg_default(PIN_XTAL2);
	nrf_gpio_cfg_output(PIN_DWM_RST);
	nrf_gpio_cfg_output(PIN_DWM_WAKE);
	nrf_gpio_cfg_input(PIN_TP4, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_VBATT_AN, NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_output(PIN_VBATT_EN);
	nrf_gpio_cfg_input(PIN_TP9, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TP10_Test, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TP11, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_NC10, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_DWM_INT, NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_output(PIN_DWM_SPI_CLK);
	nrf_gpio_cfg_input(PIN_DWM_SPI_MISO, NRF_GPIO_PIN_NOPULL);	// SPI: Note the pull setting here does nothing, see NRFX_SPI_MISO_PULL_CFG & NRFX_SPIM_MISO_PULL_CFG & NRF_SPI_DRV_MISO_PULLUP_CFG in sdk_config.h.
	nrf_gpio_cfg_output(PIN_ACCEL_SPI_MOSI);
	nrf_gpio_cfg_input(PIN_ACCEL_SPI_MISO, NRF_GPIO_PIN_NOPULL);	// SPI: Note the pull setting here does nothing, see NRFX_SPI_MISO_PULL_CFG & NRFX_SPIM_MISO_PULL_CFG & NRF_SPI_DRV_MISO_PULLUP_CFG in sdk_config.h.
	nrf_gpio_cfg_output(PIN_ACCEL_CS);
	nrf_gpio_cfg_output(PIN_DWM_SPI_MOSI);
	nrf_gpio_cfg_output(PIN_ACCEL_SPI_CLK);
	nrf_gpio_cfg_output(PIN_DWM_SPI_CS);
	nrf_gpio_cfg_input(PIN_ACCEL_INT1, NRF_GPIO_PIN_NOPULL);
	nrf_gpio_cfg_default(PIN_RESET);
	nrf_gpio_cfg_input(PIN_NC22, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_NC23, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_NC24, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TP37, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TP38, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TP39, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_TP40, NRF_GPIO_PIN_PULLDOWN);
	nrf_gpio_cfg_input(PIN_LED_GREEN, NRF_GPIO_PIN_PULLDOWN);	// Intentionally setting LED output to input to reduce current.

	nrf_gpio_cfg_output(PIN_TP42);
	nrf_gpio_cfg_input(FUNC_SEL, NRF_GPIO_PIN_PULLUP);

	// Configure output's initial state
	pinClear(PIN_DWM_RST);
	pinClear(PIN_DWM_WAKE);
	pinClear(PIN_VBATT_EN);
	pinClear(PIN_DWM_SPI_CLK);
	pinClear(PIN_ACCEL_SPI_MOSI);
	pinClear(PIN_ACCEL_CS);
	pinClear(PIN_DWM_SPI_MOSI);
	pinClear(PIN_ACCEL_SPI_CLK);
	pinClear(PIN_DWM_SPI_CS);
	//pinClear(PIN_LED_GREEN);
}

void pinSet(uint32_t pin) {
	nrf_gpio_pin_set(pin);
}

void pinClear(uint32_t pin) {
	nrf_gpio_pin_clear(pin);
}
