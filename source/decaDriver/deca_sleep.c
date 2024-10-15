/*! ----------------------------------------------------------------------------
 * @file    deca_sleep.c
 * @brief   platform dependent sleep implementation
 *
 * @attention
 *
 * Copyright 2015 (c) DecaWave Ltd, Dublin, Ireland.
 *
 * All rights reserved.
 *
 * @author DecaWave
 */

//#include <xc.h>
//#include "../mcc_generated_files/mcc.h"
#include "deca_device_api.h"
#include "nrf_delay.h"
#include <stdint.h>
//#include "port.h"

/* Wrapper function to be used by decadriver. Declared in deca_device_api.h */
void deca_sleep_EES(unsigned int time_ms)
{
    nrf_delay_ms((unsigned long)time_ms);
}

void sleep_ms_EES(unsigned long time_ms)
{
    // EES
    //__delay_ms(time_ms);
    nrf_delay_ms(time_ms);
    
    /* This assumes that the tick has a period of exactly one millisecond. See CLOCKS_PER_SEC define. */
    //unsigned long end = portGetTickCount() + time_ms;
    //while ((signed long)(portGetTickCount() - end) <= 0)
        //;
}
