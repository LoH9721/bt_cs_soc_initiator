/*******************************************************
 * Name    :CanMatrix_Def.h
 * Function:BLE 项目 CAN 信号位域定义 (严格按 DBC test_car.dbc)
 * Note    :union 模式对齐 Communi CanMatrix_Def.h
 *         DBC 信号: Intel (@0+) 字节序, 位号 = 报文绝对位
 *******************************************************/
#ifndef _CANMATRIX_DEF_H_
#define _CANMATRIX_DEF_H_

#include "../Types.h"

/*========== TX 0x2BE — BLE_KEY_INFO (DBC: BO_ 702, 100ms, 发) ==========*/
typedef union
{
    uint8_t data[8];
    struct
    {
        /* Byte[0] */
        bits_t BLE_PhoneConnect           : 1;  /* bit0:  手机连接 0=断开 1=连接 */
        bits_t BLE_PhoneAutoCmdEnable     : 1;  /* bit1:  手机自动解闭锁使能 */
        bits_t BLE_KeyConnect             : 1;  /* bit2:  钥匙连接 */
        bits_t BLE_KeyAutoCmdEnable       : 1;  /* bit3:  钥匙自动解闭锁使能 */
        bits_t reserved0_4_7             : 4;
        /* Byte[1] */
        bits_t BLE_PhoneCmd              : 2;  /* bit9:8:   手机遥控指令 (0=无,1=解锁,2=闭锁,3=寻车) */
        bits_t BLE_PhoneAutoLockCmd      : 2;  /* bit11:10: 手机自动闭锁指令 */
        bits_t BLE_PhonePos              : 3;  /* bit14:12: 手机位置区 */
        bits_t reserved1_7               : 1;  /* bit15 */
        /* Byte[2] */
        bits_t BLE_KeyCmd                : 2;  /* bit17:16: 钥匙遥控指令 */
        bits_t BLE_KeyAutoLockCmd        : 2;  /* bit19:18: 钥匙自动闭锁指令 */
        bits_t BLE_KeyPos                : 3;  /* bit22:20: 钥匙位置区 */
        bits_t reserved2_7               : 1;  /* bit23 */
        /* Byte[3] */
        bits_t BLE_PhoneRSSI             : 8;  /* bit31:24: 手机 RSSI */
        /* Byte[4] */
        bits_t BLE_PhoneDis              : 8;  /* bit39:32: 手机距离 (0~254, 255=Invalid) */
        /* Byte[5] */
        bits_t BLE_PhoneStateCode        : 8;  /* bit47:40: 手机状态码 */
        /* Byte[6] — Counter + PhoneErrCode 低 2b */
        bits_t BLE_2BE_Counter           : 4;  /* bit51:48: 报文滚动计数 */
        /* DBC: BLE_PhoneErrCode 55|8 与 Counter 55|4 重叠 — Counter 取其低4bit */
        bits_t BLE_PhoneErrCode          : 4;  /* bit55:52+: PhoneErrCode 低4bit (DBC重叠) */
        /* Byte[7] */
        bits_t BLE_2BE_CRC               : 8;  /* bit63:56: CRC-8 SAE J1850 */
    } bits;
} CanMatrix_Tx2BEMsg_Type;

/*========== RX 0x282 — VIU_BCMStsInf1 (DBC: BO_ 642, 100ms, 收) ==========*/
typedef union
{
    uint8_t data[8];
    struct
    {
        /* Byte[0] — DBC: bit2|3=BCMPowerSt */
        bits_t VIU_B_BCMPowerSt          : 3;  /* bit0-3: 电源状态 (0=Off,1=ACC,2=IGN,3=Crank) */
        bits_t reserved_bit4_5           : 2;  /* bit4-5: 未用 */
        bits_t VIU_BCMPositionLightSt    : 1;  /* bit5: 位置灯 0=OFF 1=On */
        bits_t VIU_BCMDriverDoorLockSt   : 1;  /* bit6: 主驾门锁 0=Lock 1=Unlock */
        bits_t VIU_BCMEcyLightSwhSts     : 1;  /* bit7: 危险报警灯开关 0=NotActive 1=Active */
        /* Byte[1] */
        bits_t VIU_BCMFLDoorSt           : 1;  /* bit8:  左前门 0=关 1=开 */
        bits_t VIU_BCMFRDoorSt           : 1;  /* bit9:  右前门 0=关 1=开 */
        bits_t VIU_BCMRLDoorSt           : 1;  /* bit10: 左后门 0=关 1=开 */
        bits_t VIU_BCMRRDoorSt           : 1;  /* bit11: 右后门 0=关 1=开 */
        bits_t VIU_BCMBackDoorSt         : 1;  /* bit12: 背门 */
        bits_t reserved_bit13            : 1;  /* bit13: 未用 */
        bits_t VIU_BCMLowBeamSt          : 1;  /* bit14: 近光灯 */
        bits_t VIU_BCMHighBeamSt         : 1;  /* bit15: 远光灯 */
        /* Byte[2] */
        bits_t VIU_BCMFLWindowPosition   : 7;  /* bit22:16: 左前窗位置 0~100%, 127=Invalid */
        bits_t reserved_bit23            : 1;
        /* Byte[3] */
        bits_t VIU_BCMFRWindowPosition   : 7;  /* bit30:24: 右前窗位置 */
        bits_t reserved_bit31            : 1;
        /* Byte[4] */
        bits_t VIU_BCMftFogLamptState    : 1;  /* bit32: 前雾灯 */
        bits_t VIU_BCMRrFoglamptState    : 1;  /* bit33: 后雾灯 */
        bits_t VIU_BCMLeftLightSt        : 1;  /* bit34: 左转向灯 */
        bits_t VIU_BCMRightLightSt       : 1;  /* bit35: 右转向灯 */
        bits_t VIU_BCMBrkLightSts        : 1;  /* bit36: 制动灯 */
        bits_t reserved_bit37            : 1;
        bits_t VIU_BCMDayRunlghtSts      : 1;  /* bit38: 昼行灯 */
        bits_t VIU_BCMCabinLightSts      : 1;  /* bit39: 仓顶灯 */
        /* Byte[5] */
        bits_t VIU_BCMAutoLghtSwh        : 1;  /* bit40: 自动大灯开关 */
        bits_t VIU_BCMAutoLghtSts        : 1;  /* bit41: 自动大灯状态 */
        bits_t VIU_BCMRGearLghtSts       : 1;  /* bit42: 倒车灯 */
        bits_t reserved_bit43            : 1;
        bits_t BCM_AntitheftStatus       : 2;  /* bit44-45: 防盗状态 (0=noWarning,1=Warning,2=Anti-theft,3=Reserve) */
        bits_t reserved_bit46_47         : 2;
        /* Byte[6] */
        bits_t reserved_bit48_50         : 3;
        bits_t VIU_282RC                 : 4;  /* bit51-54: Rolling Counter */
        bits_t reserved_bit55            : 1;
        /* Byte[7] */
        bits_t VIU_282CRC                : 8;  /* bit63:56: CRC-8 SAE J1850 */
    } bits;
} CanMatrix_Rx282Msg_Type;

/*========== RX 0x6DE — BLE_TEST_INFO (DBC: BO_ 1758, 100ms, GW→BLE) ==========*/
typedef union
{
    uint8_t data[8];
    struct
    {
        bits_t Diag_PhonePair            : 1;  /* bit0: 诊断配对手机 */
        bits_t Diag_KeyPair              : 1;  /* bit1: 诊断配对钥匙 */
        bits_t Diag_KeyDataReset         : 1;  /* bit2: 诊断钥匙数据复位 */
        bits_t reserved0_3_7             : 5;
        bits_t reserved1                 : 8;  /* Byte1 */
        bits_t reserved2                 : 8;  /* Byte2 */
        bits_t reserved3                 : 8;  /* Byte3 */
        bits_t reserved4                 : 8;  /* Byte4 */
        bits_t reserved5                 : 8;  /* Byte5 */
        bits_t reserved6                 : 8;  /* Byte6 */
        bits_t reserved7                 : 8;  /* Byte7 */
    } bits;
} CanMatrix_Rx6DEMsg_Type;

/*========== RX 0x61A — VIU_B_VINInfo (DBC: BO_ 1562, 1000ms, VIN分帧) ==========*/
typedef union
{
    uint8_t data[8];
    struct
    {
        bits_t VIU_VINInfoNmb           : 8;  /* bit7:0: VIN帧序号 (0=seq0, 1=seq1, 2=seq2) */
        bits_t VIU_VINData1             : 8;  /* bit15:8:   VIN数据 Byte1 */
        bits_t VIU_VINData2             : 8;  /* bit23:16:  VIN数据 Byte2 */
        bits_t VIU_VINData3             : 8;  /* bit31:24:  VIN数据 Byte3 */
        bits_t VIU_VINData4             : 8;  /* bit39:32:  VIN数据 Byte4 */
        bits_t VIU_VINData5             : 8;  /* bit47:40:  VIN数据 Byte5 */
        bits_t VIU_VINData6             : 8;  /* bit55:48:  VIN数据 Byte6 */
        bits_t VIU_VINData7             : 8;  /* bit63:56:  VIN数据 Byte7 */
    } bits;
} CanMatrix_Rx61AMsg_Type;

/* VIN 拼接缓冲区 (3帧 × 7字节 = 17字节 VIN, 帧序号: seq0/seq1/seq2) */
typedef struct
{
    uint8_t  vin[18];          /* 17 字节 VIN + 1 字节 '\0' */
    uint8_t  frame_mask;       /* bit0/1/2 → 已收到的帧序号 */
    bool     vin_valid;        /* 拼接中的 VIN 是否有效 */
    bool     complete;         /* 3 帧全部收齐 */
} CanMatrix_VinReasm_Type;

/*========== RX 0x217 — VIU_B_VIUCrlInfo2 (DBC: BO_ 535, 100ms) ==========*/
typedef union
{
    uint8_t data[8];
    struct
    {
        /* Byte[0] */
        bits_t VIU_EvpPressVolt          : 8;  /* bit7:0: 蒸发器压力传感器电压 */
        /* Byte[1] */
        bits_t VIU_EvpRelaySts           : 1;  /* bit8: 蒸发器继电器状态 */
        bits_t reserved_bit9_15          : 7;  
        // byte[2]
        bits_t reserved_bit16_23         : 8;
        // byte[3]
        bits_t FuelLeftRange_H           : 5;
        bits_t reserved_bit29_31         : 3;
        // byte[4]
        bits_t ElcLeftRange_H            : 1;
        bits_t FuelLeftRange_L           : 7;
        // byte[5]
        bits_t ElcLeftRange_M            : 8;
        // byte[6]
        bits_t VehLeftRange_H            : 5; 
        bits_t ElcLeftRange_L            : 3;
        // byte[7]
        bits_t reserved56                : 1;
        bits_t VehLeftRange_L            : 7; 
    } bits;
} CanMatrix_Rx217Msg_Type;

/* 编译期断言 */
_Static_assert(sizeof(CanMatrix_Tx2BEMsg_Type) == 8, "0x2BE: must be 8 bytes");
_Static_assert(sizeof(CanMatrix_Rx282Msg_Type) == 8, "0x282: must be 8 bytes");
_Static_assert(sizeof(CanMatrix_Rx6DEMsg_Type) == 8, "0x6DE: must be 8 bytes");
_Static_assert(sizeof(CanMatrix_Rx61AMsg_Type) == 8, "0x61A: must be 8 bytes");
_Static_assert(sizeof(CanMatrix_Rx217Msg_Type) == 8, "0x217: must be 8 bytes");

#endif /* _CANMATRIX_DEF_H_ */
