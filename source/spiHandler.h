
#ifndef SPI_HANDLER_H
#define SPI_HANDLER_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <nrfx.h>
#include "nrf_drv_spi.h"

#define SPI_WRITE_CMD   0x00
#define SPI_READ_CMD    0x80
#define SPI_EMPTY	0xFF

#define SPI_READ_BYTES_MAX	255

#define PIN_DWM_SPI_CS		19
#define PIN_ACCEL_CS		16

#define SPI0_MISO_PIN          15
#define SPI0_MOSI_PIN          14
#define SPI0_SCK_PIN           18

#define SPI1_MISO_PIN          13
#define SPI1_MOSI_PIN          17
#define SPI1_SCK_PIN           12

typedef enum {
	SPI_LIS2DW12 = 0,
	SPI_DECAWAVE = 1
} SpiSelect_t;


// Public Function Declarations
void spiSetFrequency(SpiSelect_t select, nrf_drv_spi_frequency_t frequency);
void spiEnable(SpiSelect_t select, nrf_drv_spi_mode_t mode);
void spiDisable(SpiSelect_t select);
void spiWriteByte(SpiSelect_t select, uint8_t byte, nrf_drv_spi_mode_t mode);
void spiWriteBytes(SpiSelect_t select, const uint8_t *bytes, uint8_t size, nrf_drv_spi_mode_t mode);
void spiWriteBytesAsSingleBytes(SpiSelect_t select, const uint8_t *bytes, uint8_t size, nrf_drv_spi_mode_t mode);
void spiReadArray(SpiSelect_t select, uint8_t *txBytes, uint8_t *rxBytes, uint8_t totalSize, nrf_drv_spi_mode_t mode);
void spiReadArrayAsSingleBytes(SpiSelect_t select, uint8_t *txBytes, uint8_t *rxBytes, uint8_t totalSize, nrf_drv_spi_mode_t mode);

#endif
