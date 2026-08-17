/*******************************************************
 * Name    :Memory.c
 * Function:Memory utility implementations for BLE project
 *******************************************************/
#include "Memory.h"

void Memory_Fill(uint8_t *p_buff, uint8_t data, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++)
    {
        p_buff[i] = data;
    }
}

void Memory_Copy(volatile uint8_t *dst, const uint8_t *src, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++)
    {
        dst[i] = src[i];
    }
}

bool Memory_Compare(const uint8_t *p_des, const uint8_t *p_src, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < len; i++)
    {
        if (p_des[i] != p_src[i])
        {
            return true; /* different */
        }
    }
    return false; /* equal */
}
