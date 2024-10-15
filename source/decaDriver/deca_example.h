/* 
 * File:   utility.h
 * Author: Anthony
 *
 * Created on January 9, 2020, 1:21 PM
 */

#ifndef DECA_EXAMPLE_H
#define	DECA_EXAMPLE_H

#include <stdio.h>
#include <string.h>
//#include <xc.h>

// LCD redirect
#define lcd_display_str(x) printf(x)

void tx_main(void * arg);
void rx_main(void * arg);

// Stubs
void peripherals_init();
void spi_set_rate_low();
void spi_set_rate_high();

#endif	/* DECA_EXAMPLE_H */

