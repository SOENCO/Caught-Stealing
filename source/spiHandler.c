
#include "spiHandler.h"

#include "app_error.h"
#include "app_scheduler.h"
#include "app_util_platform.h"
#include "nrf_gpio.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "sdk_config.h"
#include <string.h>
#include "utility.h"


// NOTE: Enabling SPI0_USE_EASY_DMA in sdk_config allows for consecutive bytes to be sent without delay between clocks, but some SPI chips have an issue with back-to-back bytes with multibyte commands. Also nRF52 SPI with DMA sends an extra 0xFF byte with 1 byte transfers.

// Module Init
#define SPI_0_INSTANCE_ID	0
#define SPI_1_INSTANCE_ID	1
static const nrf_drv_spi_t spi0 = NRF_DRV_SPI_INSTANCE(SPI_0_INSTANCE_ID);
static const nrf_drv_spi_t spi1 = NRF_DRV_SPI_INSTANCE(SPI_1_INSTANCE_ID);

// Public variables

// Private variables

static nrf_drv_spi_config_t config0 = {
	.sck_pin		= SPI0_SCK_PIN,
	.mosi_pin		= SPI0_MOSI_PIN,
	.miso_pin		= SPI0_MISO_PIN,
	.ss_pin			= NRF_DRV_SPI_PIN_NOT_USED,
	.irq_priority	= APP_IRQ_PRIORITY_LOWEST,
	.orc			= 0xFF,
	.frequency		= NRF_SPI_FREQ_4M,	// TODO: see if NRF_SPI_FREQ_8M works. Datasheet allows for upto 10M.
	.mode           = NRF_DRV_SPI_MODE_0,
	.bit_order      = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST
};

static nrf_drv_spi_config_t config1 = {
	.sck_pin		= SPI1_SCK_PIN,
	.mosi_pin		= SPI1_MOSI_PIN,
	.miso_pin		= SPI1_MISO_PIN,
	.ss_pin			= NRF_DRV_SPI_PIN_NOT_USED,
	.irq_priority	= APP_IRQ_PRIORITY_LOWEST,
	.orc			= 0xFF,
	.frequency		= NRF_SPI_FREQ_2M,		// Must be <3 MHz before calling dwt_initialise(), according to DW1000_Software_API_Guide_rev2p7.pdf page 20.
	.mode           = NRF_DRV_SPI_MODE_0,
	.bit_order      = NRF_DRV_SPI_BIT_ORDER_MSB_FIRST
};

// Private Function Declarations
static const nrf_drv_spi_t *spi(SpiSelect_t select);


// Private Function Definitions
static const nrf_drv_spi_t *spi(SpiSelect_t select) {
	return (select == SPI_LIS2DW12) ? &spi0 : &spi1;
}


// Public Function Definitions
void spiSetFrequency(SpiSelect_t select, nrf_drv_spi_frequency_t frequency) {
	// Don't change frequency while transmitting/receiving.
	spiDisable(select);

	nrf_drv_spi_config_t *config = (select == SPI_LIS2DW12) ? &config0 : &config1;
	config->frequency = frequency;

	spiEnable(select, config->mode);
	// spi(select)->u.spi.p_reg->FREQUENCY = frequency;
}

void spiEnable(SpiSelect_t select, nrf_drv_spi_mode_t mode) {
	nrf_drv_spi_config_t *config = (select == SPI_LIS2DW12) ? &config0 : &config1;

	if (!(spi(select)->u.spi.p_reg->ENABLE) || (config->mode != mode)) {
		spiDisable(select);
		nrf_delay_us(1);

		config->mode = mode;
		ret_code_t err_code = nrf_drv_spi_init(spi(select), config, NULL, NULL);
		APP_ERROR_CONTINUE(err_code);
	}
}

void spiDisable(SpiSelect_t select) {
	if (spi(select)->u.spi.p_reg->ENABLE) {
		nrf_drv_spi_uninit(spi(select));
	}
}

void spiWriteByte(SpiSelect_t select, uint8_t byte, nrf_drv_spi_mode_t mode) {
	uint8_t bytes[1] = { byte };
	spiWriteBytes(select, bytes, 1, mode);
}

void spiWriteBytes(SpiSelect_t select, const uint8_t *bytes, uint8_t size, nrf_drv_spi_mode_t mode) {
	spiEnable(select, mode);

	// Blocking
	ret_code_t err_code = nrf_drv_spi_transfer(spi(select), bytes, size, NULL, 0);
	APP_ERROR_CONTINUE(err_code);
}

void spiWriteBytesAsSingleBytes(SpiSelect_t select, const uint8_t *bytes, uint8_t size, nrf_drv_spi_mode_t mode) {
	spiEnable(select, mode);

	// Blocking
	ret_code_t err_code;
	for (uint8_t index = 0; index < size; index++) {
		err_code = nrf_drv_spi_transfer(spi(select), &bytes[index], 1, NULL, 0);
		APP_ERROR_CONTINUE(err_code);
	}
}

void spiReadArray(SpiSelect_t select, uint8_t *txBytes, uint8_t *rxBytes, uint8_t totalSize, nrf_drv_spi_mode_t mode) {
	// Sends all bytes together without gaps between each. 'totalSize' should equal the total clocks needed to send the tx and receive the rx.
	spiEnable(select, mode);

	ret_code_t err_code = nrf_drv_spi_transfer(spi(select), txBytes, totalSize, rxBytes, totalSize);
	APP_ERROR_CONTINUE(err_code);
}

void spiReadArrayAsSingleBytes(SpiSelect_t select, uint8_t *txBytes, uint8_t *rxBytes, uint8_t totalSize, nrf_drv_spi_mode_t mode) {
	// Some chips do not like back to back bytes, this sends individually. 'totalSize' should equal the total clocks needed to send the tx and receive the rx.
	spiEnable(select, mode);

	ret_code_t err_code;
	for (uint8_t index = 0; index < totalSize; index++) {
		err_code = nrf_drv_spi_transfer(spi(select), &txBytes[index], 1, &rxBytes[index], 1);
		APP_ERROR_CONTINUE(err_code);
	}
}
