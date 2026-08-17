/*******************************************************
  * Name    :CanNm_Cfg.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2022.5.20
*******************************************************/
#ifndef _CANNM_CFG_H_
#define _CANNM_CFG_H_
/*-------------include file---------------------------*/
#include "../Types.h"
#include "../Memory.h"
#include "../RteSys.h"
#include "../user_can_config.h"
/*-------------extern declarations---------------------*/
extern void    CanNm_SetTxMsgOKFlag(void);
extern Bool    CanNM_CheckIfAllowTxMsg(void);
extern void    CanNm_StartTxMsg(void);
extern void    CanNm_StopTxMsg(void);
extern void    CanIF_SwtichAppUdsCtlByNm(Bool isRxMsg, Bool isEnable);
extern Bool    CanIf_ReadNmRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool    CanIf_WriteNmTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern void    CanIf_ClearAllRxAndTxBuff(void);
extern void    CanTransciever_ClearEnableTxAppMsg(void);
#if CAN_UDS_ENABLE
extern uint8_t  Uds_GetCurrentSessionMode(void);
#endif
/*-------------define---------------------------------*/
#define CANNM_BASE_ID                                 0x400
#define CANNM_ID                                      0x424
#define CANNM_N_IMMEDIATE_CYCLE_TIMES                 10
#define CANNM_T_IMMEDIATE_CYCLE_TIME                  20
#define CANNM_T_MESSAGE_CYCLE_TIME                    500
#define CANNM_T_TIMEOUT_TIME                          2000
#define CANNM_T_REPEAT_MESSAGE_TIME                   1500
#define CANNM_T_WAIT_BUS_SLEEP_TIME                   2000
#define CANNM_T_WAKEUP_TIMEOUT                        1000
#define CANNM_T_DIAG_TIMEOUT                          5000
/*-------------function port--------------------------*/
/*-------------RteSys port----------------------------*/
#define CanNm_GetDiagRequestFlag()                    RteSys_GetBoolSig(RTESYS_BOOL_DIAG_REQUEST_FLAG)
#define CanNm_GetCanWakeUpFlag()                      RteSys_GetBoolSig(RTESYS_BOOL_CAN_REASON_FLAG)
#define CanNm_SetDiagRequestFlag(flag)                RteSys_SetBoolSig(RTESYS_BOOL_DIAG_REQUEST_FLAG, (flag))
#define CanNm_SetDCanWakeUpFlag(flag)                 RteSys_SetBoolSig(RTESYS_BOOL_CAN_REASON_FLAG, (flag))
/*-------------Uds port-------------------------------*/
#if CAN_UDS_ENABLE
#define CanNm_GetCurrentSessionMode()                 Uds_GetCurrentSessionMode()
#else
#define CanNm_GetCurrentSessionMode()                 (0)
#endif
/*-------------CanIF port-----------------------------*/
#define CanNm_SwtichAppUdsCtlMsg(isRxMsg, isEnable)   CanIF_SwtichAppUdsCtlByNm(isRxMsg, isEnable)
#define CanNm_ReadRxMsg(p_frame, p_id, p_buff, p_len) CanIf_ReadNmRxMsg(p_frame, p_id, p_buff, p_len)
#define CanNm_WriteTxMsg(frame, id, p_buff, len)      CanIf_WriteNmTxMsg(frame, id, p_buff, len)
#define CanNm_ClearRxAndTxBuff()                      CanIf_ClearAllRxAndTxBuff()
/*-------------CanTransciver port---------------------*/
#define Cannm_ClearEnableTxApp()                      CanTransciever_ClearEnableTxAppMsg()
/*-------------Memory port-----------------------------*/
#define CanNm_Fill(p_buff, data, len)                 Memory_Fill(p_buff, data, len)
/*-------------enum and struct------------------------*/
typedef enum
{
    CANNM_OFF_STATE              = 0x00,
    CANNM_BUS_SLEEP_MODE         = 0x01,
    CANNM_REPEAT_MESSAGE_STATE   = 0x02,
    CANNM_NORMAL_OPERATION_STATE = 0x03,
    CANNM_READY_SLEEP_STATE      = 0x04,
    CANNM_PREPARE_BUS_SLEEP_MODE = 0x05
} CanNm_State_Type;

typedef enum
{
    CANNM_RX_NOT_REPEAT_PASSIVE = 0x00,
    CANNM_RX_NOT_REPEAT_ACTIVE  = 0x10,
    CANNM_RX_REPEAT_PASSIVE     = 0x01,
    CANNM_RX_REPEAT_ACTIVE      = 0x11,
    CANNM_RX_NULL               = 0xFF
} CanNm_RxType_Type;

typedef enum
{
    CANNM_TX_NOT_REPEAT_PASSIVE = 0x00,
    CANNM_TX_NOT_REPEAT_ACTIVE  = 0x10,
    CANNM_TX_REPEAT_PASSIVE     = 0x01,
    CANNM_TX_REPEAT_ACTIVE      = 0x11,
    CANNM_TX_NULL               = 0x04
} CanNm_TxType_TYPE;

typedef struct
{
    Bool    workIsOn;
    Bool    rxMsgFlag;
    Bool    txMsgFlag;
    Bool    localRequest;
    Bool    diagRequest;
    Bool    wakeupKeep;
    uint8_t immediateTimes;
} CanNm_Data_Type;

typedef union
{
    uint8_t data;
    struct
    {
        bits_t timerout     : 1;
        bits_t msgcycle     : 1;
        bits_t repeatmsg    : 1;
        bits_t waitbussleep : 1;
        bits_t wakeuptime   : 1;
        bits_t diagtime     : 1;
        bits_t resverd      : 2;
    } bits;
} CanNm_TimeCtr_Type;

typedef struct
{
    uint16_t timerout;
    uint16_t msgcycle;
    uint16_t repeatmsg;
    uint16_t waitbussleep;
    uint16_t wakeuptime;
    uint16_t diagtime;
} CanNm_TimeTick_Type;

typedef union
{
    uint8_t data[8];
    struct
    {
        //Byte[0]
        bits_t NFC_PE_SourceNodeIdentifier    : 8;
        //Byte[1]
        bits_t NFC_PE_RepeatMessageRequestBit : 1;
        bits_t reserved9_11                   : 3;
        bits_t NFC_PE_ActiveWakeupBit         : 1;
        bits_t reserved13_15                  : 3;
        //Byte[2]
        bits_t NFC_PE_RepeatSts               : 1;
        bits_t reserved17_20                  : 4;
        bits_t NFC_PE_NMReq_NM                : 1;
        bits_t NFC_PE_NMReq_Diag              : 1;
        bits_t NFC_PE_NMReq_Poweron           : 1;
        //Byte[3]
        bits_t reserved24_30                  : 7;
        bits_t NFC_PE_NMReq_DetectNFC         : 1;
        //Byte[4]
        bits_t reserved32_39                  : 8;
        //Byte[5]
        bits_t reserved40_47                  : 8;
        //Byte[6]
        bits_t NFC_PE_FirstWakeupReason       : 6;
        bits_t reserved54_55                  : 2;
        //Byte[7]
        bits_t reserved56_63                  : 8;
    } bits;
} CanNm_Msg_Type;
#endif
