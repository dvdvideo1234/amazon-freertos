/*
 * entropy.c
 *
 *  Created on: 6 Dec 2017
 *      Author: chris
 */

#include "FreeRTOS.h"
#include "FreeRTOSIPConfig.h"
#include "mbedtls/entropy_poll.h"

int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len, size_t *olen)
{
    /* No context needed */
    (void)data;
    /* Fill output buffer */
    for (uint32_t i = 0, tmp_rnd = 0; i < len; i++)
    {
        /* Get 4B random number */
        if ((i & 0x3) == 0)
            tmp_rnd = uxRand();
        output[i] = ((uint8_t*)&tmp_rnd)[i & 0x3];
    }
    *olen = len;
    return 0;
}
