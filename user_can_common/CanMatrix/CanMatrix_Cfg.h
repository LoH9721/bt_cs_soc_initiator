/*******************************************************
 * Name    :CanMatrix_Cfg.h
 * Function:BLE CAN matrix config — merges Communi types
 *          (needed by CanMatrix.c) + BLE RTE API
 *          (needed by CanMatrix_Cfg.c)
 * Note    :信号位域在 CanMatrix_Def.h (严格按 DBC)
 *******************************************************/
#ifndef _CANMATRIX_CFG_H_
#define _CANMATRIX_CFG_H_

#include "../Types.h"
#include "../Memory.h"
#include "../RteSys.h"
#include "CanMatrix_Def.h"

/*-------------message counts (按 DBC 报文数)--------------*/
#define CANMATRIX_DTC_LIST_NUM        1    /* minimal for CanMatrix.c compatibility */
#define CANMATRIX_RX_LIST_NUM         4    /* 0x282 + 0x6DE + 0x61A + 0x217 */
#define CANMATRIX_TX_LIST_NUM         1    /* 0x2BE */
#define CANMATRIX_CTR_DTC_TIMES       1
#define CANMATRIX_RX_SCHEDULE_TIME    8    /* ms */
#define CANMATRIX_TX_SCHEDULE_TIME    2    /* ms */

/*-------------extern declarations for macro targets----*/
extern Bool CanIf_ReadAppRxMsg(Frame_Type *, uint32_t *, volatile uint8_t *, uint8_t *);
extern Bool CanIf_WriteAppTxMsg(Frame_Type, uint32_t, volatile uint8_t *, uint8_t);
extern Bool CanNM_CheckIfAllowTxMsg(void);
extern Bool CanTransciever_CheckStartAppTimerIsOk(void);

/*-------------port macros (for CanMatrix.c)-----------*/
#define CanMatrix_SetDtcCurErrFlag(id, flag)      ((void)(id),(void)(flag))
#define CanMatrix_GetDtcDeactiveFlag()            RteSys_GetDtcDeactiveFlag()
#define CanMatrix_CheckIfAllowRecordDTC()         FALSE
#define CanMatrix_CheckIsInNoAck()                FALSE
#define CanMatrix_CheckIsInBusOff()               FALSE
#define CanMatrix_Compare(p_des, p_src, len)      Memory_Compare(p_des, p_src, len)
#define CanMatrix_Copy(p_buff, p_src, len)        Memory_Copy((volatile uint8_t *)(p_buff), (const uint8_t *)(p_src), (uint8_t)(len))
#define CanMatrix_Fill(p_buff, data, len)         Memory_Fill(p_buff, data, len)
#define CanMatrix_ReadRxMsg(p_f,p_i,p_b,p_l)      CanIf_ReadAppRxMsg(p_f,p_i,p_b,p_l)
#define CanMatrix_WriteTxMsg(f,i,b,l)              CanIf_WriteAppTxMsg(f,i,b,l)
#define CanMatrix_CheckStartAppTimerIsOk()         CanTransciever_CheckStartAppTimerIsOk()
#define CanMatrix_CheckNMIfAllowTxMsg()            CanNM_CheckIfAllowTxMsg()

/*-------------Communi state enums (for CanMatrix.c)---*/
typedef enum {
    CANMATRIX_STATE_INIT   = 0x00,
    CANMATRIX_STATE_NORMAL = 0x01,
    CANMATRIX_STATE_OFF    = 0x02
} CanMatrix_State_Type;

typedef enum {
    CANMATRIX_RX_STATE_INIT   = 0x00,
    CANMATRIX_RX_STATE_NORMAL = 0x01,
    CANMATRIX_RX_STATE_LOST   = 0x02
} CanMatrix_RxState_Type;

typedef enum {
    CANMATRIX_TX_STATE_CHECK  = 0x00,
    CANMATRIX_TX_STATE_UPDATE = 0x01,
    CANMATRIX_TX_STATE_CLEAR  = 0x02
} CanMatrix_TxState_Type;

/*-------------handler function types------------------*/
typedef void    (*CanMatrix_DtcHandle_Fun)(void);
typedef void    (*CanMatrix_RxHandle_Fun)(uint8_t state);
typedef uint8_t (*CanMatrix_TxHandle_Fun)(uint8_t state);

/*-------------info types (for CanMatrix.c)------------*/
typedef struct {
    Bool     flag;
    uint8_t  tick;
    uint16_t time;
} CanMatrix_DtcMsgInfo_TYPE;

typedef struct {
    Bool     flag;
    Bool     lost;
    uint16_t time;
} CanMatrix_RxMsgInfo_Type;

typedef struct {
    Bool     txFlag;
    uint8_t  txTimes;
    uint32_t lastTxTime;        /* 周期 / 事件间隔 (真实 1ms, RteSys_GetSysTimeMs) */
    uint32_t lastFastTime;      /* 快发间隔 (EVENT / CYCEV, 真实 1ms) */
} CanMatrix_TxMsgInfo_Type;

/*-------------config types (compatible with both .c)---*/
typedef struct {
    uint32_t               id;
    uint16_t               lostTime;
    Bool                  *lost;
    CanMatrix_DtcHandle_Fun dataHandleFun;
} CanMatrix_DtcList_Type;

typedef struct {
    Frame_Type             frame;
    uint32_t               id;
    uint16_t               lostTime;
    uint8_t                len;
    Bool                   IsChangeIn;
    uint8_t               *p_buff;
    CanMatrix_RxHandle_Fun dataHandleFun;
} CanMatrix_RxList_Type;

typedef struct {
    Frame_Type             frame;
    uint32_t               id;
    uint8_t                fastPeroid;
    uint16_t               peroid;
    uint8_t                len;
    Msg_Type               type;
    uint8_t               *p_buff;
    CanMatrix_TxHandle_Fun dataHandleFun;
} CanMatrix_TxList_Type;

/*-------------table variable declarations--------------*/
extern CanMatrix_DtcList_Type g_CanMatrixDtcList[CANMATRIX_DTC_LIST_NUM];
extern CanMatrix_RxList_Type  g_CanMatrixRxList[CANMATRIX_RX_LIST_NUM];
extern CanMatrix_TxList_Type  g_CanMatrixTxList[CANMATRIX_TX_LIST_NUM];

/*-------------init/API declarations-------------------*/
extern void CanMatrix_DefaultDataInit(void);
extern void CanMatrix_Init(void);
extern void CanMatrix_TxMsgMain(void);
extern void CanMatrix_RxMsgMain(void);
extern Bool CanMatrix_CheckIfRxMsg(uint32_t id);

/*============= BLE RTE layer =========================*/
typedef enum {
    RTE_282_BCM_PowerSt, RTE_282_BCM_FLDoorSt, RTE_282_BCM_FRDoorSt,
    RTE_282_BCM_AntitheftStatus, RTE_282_BCM_LostFlag,
    RTE_282_BCM_DriverDoorLockSt, RTE_282_BCM_BackDoorSt,
    RTE_2BE_Phone_Connect, RTE_2BE_Phone_AutoEn, RTE_2BE_Phone_Cmd,
    RTE_2BE_Phone_Pos, RTE_2BE_Phone_LockCmd,
    RTE_2BE_Phone_RSSI, RTE_2BE_Phone_Dis,
    RTE_2BE_Key_Connect, RTE_2BE_Key_AutoEn, RTE_2BE_Key_Cmd,
    RTE_2BE_Key_Pos, RTE_2BE_Key_LockCmd,
    RTE_6DE_Diag_PhonePair, RTE_6DE_Diag_KeyPair, RTE_6DE_Diag_KeyDataReset,
    RTE_VIU_VIN_VALID, RTE_VIU_PEPS_KEY_IN_CAR,
    RTE_VIU_DOOR_RL, RTE_VIU_DOOR_RR,
    RTE_VIU_FINAL_LOCK_STATE, RTE_VIU_REMAINING_RANGE_H, RTE_VIU_REMAINING_RANGE_L,
    RTE_BG24_VIN_ASSOC_STATE,
    RTE_CAN_SIG_NUM
} RTE_CAN_ID;

void    user_can_rte_write_canSig(RTE_CAN_ID id, uint8_t sig);
uint8_t user_can_rte_read_canSig(RTE_CAN_ID id);

/*-------------AppProto zone/cmd enums (from old user_can_app_protocol.h) */
typedef enum {
    APP_PROTO_ZONE_DISCONNECTED_UNKNOWN = 0,
    APP_PROTO_ZONE_IN_CAR               = 1,
    APP_PROTO_ZONE_OUTSIDE_UNLOCK       = 2,
    APP_PROTO_ZONE_OUTSIDE_LOCK         = 3,
    APP_PROTO_ZONE_PARKING_INVALID      = 4
} AppProto_Zone_Type;

typedef enum {
    APP_PROTO_REMOTE_CMD_NONE   = 0,
    APP_PROTO_REMOTE_CMD_UNLOCK = 1,
    APP_PROTO_REMOTE_CMD_LOCK   = 2,
    APP_PROTO_REMOTE_CMD_FIND   = 3
} AppProto_RemoteCmd_Type;

/*-------------BLE -> CAN bridge API (external)--------*/
void    AppProto_Key_SetConnected(bool connected);
void    AppProto_Key_SetCmd(uint8_t cmd);
void    AppProto_Key_SetPos(uint8_t pos);
void    AppProto_Key_SetLockCmd(uint8_t cmd);
void    AppProto_Key_SetAutoEnable(bool enabled);
void    AppProto_Phone_SetConnected(bool connected);
void    AppProto_Phone_SetCmd(uint8_t cmd);
void    AppProto_Phone_SetPos(uint8_t pos);
void    AppProto_Phone_SetLockCmd(uint8_t cmd);
void    AppProto_Phone_SetAutoEnable(bool enabled);
void    AppProto_Phone_SetRssi(uint8_t rssi);
void    AppProto_Phone_SetDis(uint8_t dis);
uint8_t AppProto_GetVehiclePowerSt(void);
uint8_t AppProto_GetVehicleDoors(void);
uint8_t AppProto_GetAntitheftStatus(void);

#endif /* _CANMATRIX_CFG_H_ */
