/*******************************************************
 * Name    :user_can_types.h
 * Function:CAN 通信层基础类型定义
 * Note    :移植自 NetTypes.h，去掉 LIN 相关
*******************************************************/
#ifndef _USER_CAN_TYPES_H_
#define _USER_CAN_TYPES_H_

#include <stdint.h>
#include <stdbool.h>

/*-------------typedef--------------------------------*/
typedef bool Bool;
#define FALSE false
#define TRUE  true

typedef enum
{
    CAN = 0,
    CANFD
} Frame_Type;

typedef enum
{
    EVENT = 0x00,
    CYCLE,
    CYCEV
} Msg_Type;

typedef enum
{
    RET_OK      = 0x00,
    RET_NOT_OK,
    RET_RCRRP,
    RET_PENDING
} Return_Type;

typedef enum
{
    CRC_16 = 0,
    CRC_32 = 1
} Crc_Type;

typedef struct
{
    Frame_Type frame;
    uint16_t   id;
    uint8_t    len;
    uint8_t    buff[64];
} Can_Type;

#endif
