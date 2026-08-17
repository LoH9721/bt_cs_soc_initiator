/***************************************************************************//**
 * @file user_security.c
 * @brief CRC16-CCITT-FALSE、安全内存清零、常量时间比较。
 ******************************************************************************/

#include "user_security.h"

uint16_t user_security_crc16(const uint8_t *data, size_t len)
{
  uint16_t crc = 0xFFFFU;
  size_t i;
  int b;

  if (data == NULL || len == 0U) {
    return crc;
  }
  for (i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (b = 0; b < 8; b++) {
      if ((crc & 0x8000U) != 0U) {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

void user_security_init(void)
{
}

void user_security_memzero(void *buf, size_t len)
{
  if (buf == NULL || len == 0U) {
    return;
  }
  volatile uint8_t *p = (volatile uint8_t *)buf;
  while (len != 0U) {
    *p = 0U;
    p++;
    len--;
  }
}

bool user_security_const_time_mem_eq(const uint8_t *a, const uint8_t *b, size_t len)
{
  size_t i;
  uint8_t diff = 0U;

  if (a == NULL || b == NULL) {
    return false;
  }
  for (i = 0; i < len; i++) {
    diff |= (uint8_t)(a[i] ^ b[i]);
  }
  return diff == 0U;
}
