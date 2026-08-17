/*******************************************************
 * Name    :Memory.h
 * Function:Memory utility stubs for BLE project
 *          (replaces Communi Memory.h)
 *******************************************************/
#ifndef _MEMORY_H_
#define _MEMORY_H_

#include <stdint.h>
#include <stdbool.h>

extern void Memory_Fill(uint8_t *p_buff, uint8_t data, uint8_t len);
extern void Memory_Copy(volatile uint8_t *dst, const uint8_t *src, uint8_t len);
extern bool Memory_Compare(const uint8_t *p_des, const uint8_t *p_src, uint8_t len);

/* Aliases used by Communi CanTp */
#define Memory_CopyShort(p_buff, p_src, len)  Memory_Copy((volatile uint8_t *)(p_buff), (const uint8_t *)(p_src), (uint8_t)(len))
#define Memory_FillLong(p_buff, data, len)    Memory_Fill((uint8_t *)(p_buff), (uint8_t)(data), (uint8_t)(len))

#endif /* _MEMORY_H_ */
