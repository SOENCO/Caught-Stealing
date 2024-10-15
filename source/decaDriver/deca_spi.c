/*! ----------------------------------------------------------------------------
 * @file	deca_spi.c
 * @brief	SPI access functions
 *
 * @attention
 *
 * Copyright 2013 (c) DecaWave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author DecaWave
 */
#include <string.h>
#include <stdint.h>
#include "deca_spi.h"
#include "deca_device_api.h"
#include "nrf_delay.h"
#include "nrf_log.h"
#include "spiHandler.h"
#include "nrf_gpio.h"
#include "gpio.h"
#include "utility.h"

#define CS_LOW  false
#define CS_HIGH true

void inline chip_select(bool high)
{
//    if (high)
//    {
//        LATBbits.LATB5 = 1;
//    }
//    else
//    {
//        LATBbits.LATB5 = 0;
//        LATBbits.LATB2 = 1;
//    }
}

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: openspi()
 *
 * Low level abstract function to open and initialise access to the SPI device.
 * returns 0 for success, or -1 for error
 */
int openspi(/*SPI_TypeDef* SPIx*/)
{
	// done by port.c, default SPI used is SPI1

	return 0;

} // end openspi()

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: closespi()
 *
 * Low level abstract function to close the the SPI device.
 * returns 0 for success, or -1 for error
 */
int closespi(void)
{
    // TODO: EES
	//while (port_SPIx_busy_sending()); //wait for tx buffer to empty

	//port_SPIx_disable();

	return 0;

} // end closespi()

/*! ------------------------------------------------------------------------------------------------------------------
 * Function: writetospi()
 *
 * Low level abstract function to write to the SPI
 * Takes two separate byte buffers for write header and write data
 * returns 0 for success, or -1 for error
 */
//#pragma GCC optimize ("O3")
int writetospi(uint16 headerLength, const uint8 *headerBuffer, uint32 bodylength, const uint8 *bodyBuffer)
{
    pinClear(PIN_DWM_SPI_CS);
	spiWriteBytes(SPI_DECAWAVE, headerBuffer, headerLength, NRF_DRV_SPI_MODE_0);
	spiWriteBytes(SPI_DECAWAVE, bodyBuffer, bodylength, NRF_DRV_SPI_MODE_0);
    pinSet(PIN_DWM_SPI_CS);
    return 0;
} // end writetospi()


/*! ------------------------------------------------------------------------------------------------------------------
 * Function: readfromspi()
 *
 * Low level abstract function to read from the SPI
 * Takes two separate byte buffers for write header and read data
 * returns the offset into read buffer where first byte of read data may be found,
 * or returns -1 if there was an error
 */
//#pragma GCC optimize ("O3")
int readfromspi(uint16 headerLength, const uint8 *headerBuffer, uint32 readlength, uint8 *readBuffer)
{
	uint8_t txBytes[readlength];
	memset(txBytes, 0xFF, sizeof(txBytes));

    pinClear(PIN_DWM_SPI_CS);
	spiWriteBytes(SPI_DECAWAVE, headerBuffer, headerLength, NRF_DRV_SPI_MODE_0);
	spiReadArray(SPI_DECAWAVE, txBytes, readBuffer, readlength, NRF_DRV_SPI_MODE_0);

    pinSet(PIN_DWM_SPI_CS);
    return 0;
}
