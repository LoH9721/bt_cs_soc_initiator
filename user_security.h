/***************************************************************************//**
 * @file user_security.h
 * @brief 安全相关算法与工具：CRC16-CCITT-FALSE、敏感数据清零、常量时间比较等。
 ******************************************************************************/
#ifndef USER_SECURITY_H
#define USER_SECURITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

uint16_t user_security_crc16(const uint8_t *data, size_t len);
void user_security_init(void);
void user_security_memzero(void *buf, size_t len);
bool user_security_const_time_mem_eq(const uint8_t *a, const uint8_t *b, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* USER_SECURITY_H */
