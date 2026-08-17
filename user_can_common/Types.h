/*******************************************************
 * Name    :Types.h
 * Function:基础类型定义 (适配 Communi 模块到 BLE 项目)
 *******************************************************/
#ifndef _TYPES_H_
#define _TYPES_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

typedef bool Bool;
#define FALSE false
#define TRUE  true

typedef enum { CAN = 0, CANFD } Frame_Type;
typedef enum { EVENT = 0x00, CYCLE, CYCEV } Msg_Type;
typedef enum { RET_OK = 0x00, RET_NOT_OK, RET_RCRRP, RET_PENDING } Return_Type;
typedef enum { CRC_16 = 0, CRC_32 = 1 } Crc_Type;

typedef struct {
    Frame_Type frame;
    uint16_t   id;
    uint8_t    len;
    uint8_t    buff[64];
} Can_Type;

/* bit-field type (Communi 通用) */
typedef uint8_t bits_t;

#endif
