/*******************************************************
  * Name    :CanTransciever_Cfg.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _CANTRANSCIEVER_CFG_H_
#define _CANTRANSCIEVER_CFG_H_
/*-------------include file---------------------------*/
#include "../Types.h"
#include "../Rte.h"
#include "../RteSys.h"
/*-------------TCAN4550 extern declarations------------*/
extern void TCAN_Init(void);
extern void TCAN_Enable(void);
extern void TCAN_Disable(void);
extern void TCAN_OpenInterrupt(void);
extern void TCAN_CloseInterrupt(void);
extern void TCAN_TransceiverOn(void);
extern void TCAN_TransceiverOff(void);
extern void TCAN_SendAFrame(uint16_t id, const uint8_t *data, uint8_t len);
extern int TCAN_GetSendResult(void);
extern void TCAN_ClearMailbox(void);
extern bool TCAN_GetNoAckFlag(void);
extern void TCAN_ClearNoAckFlag(void);
extern bool TCAN_GetBusOffFlag(void);
extern void TCAN_ClearBusOffFlag(void);
extern uint8_t TCAN_GetBusOffCounter(void);
extern void TCAN_ClearBusOffCounter(void);
extern bool TCAN_CheckIfReceive(void);
extern void TCAN_ReadReceivedFrame(uint16_t *id, uint8_t *data, uint8_t *len);
/*-------------CanNm extern declarations---------------*/
extern void CanNm_SetTxMsgOKFlag(void);
extern Bool CanNM_CheckIfAllowTxMsg(void);
extern void CanNm_StartTxMsg(void);
extern void CanNm_StopTxMsg(void);
/*-------------CanMatrix extern declarations-----------*/
extern void CanMatrix_TxMsgMain(void);
extern Bool CanMatrix_CheckIfRxMsg(uint32_t id);
/*-------------CanIf extern declarations---------------*/
extern void CanIf_ClearAllRxAndTxBuff(void);
extern void CanIF_SwtichAppUdsCtlByNm(Bool isRxMsg, Bool isEnable);
extern Bool CanIf_WriteAppRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_WriteNmRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_WriteUdsRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_ReadAppTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_ReadNmTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_ReadUdsTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
/*-------------NoAck/BusOff functions------------------*/
extern Bool CanTransciever_IsInNoAck(void);
extern Bool CanTransciever_IsInBusOff(void);
/*-------------CAN ID defines (BLE project DBC)--------*/
#define CANNM_BASE_ID                                              0x400
#define CANNM_ID                                                   0x424
#define UDS_FUNC_ADDR_ID                                           0x7DF
#define UDS_PHYS_ADDR_ID                                           0x7E0
/*-------------define---------------------------------*/
#define CANTRANSCIEVER_BUSOFF_DTC_CNT                             10
#define CANTRANSCIEVER_MAX_START_APP_TIME                         100
#define CANTRANSCIEVER_MAX_TX_MSG_CNT                             1
#define CANTRANSCIEVER_START_UDS_TIME                             1700
#define CANTRANSCIEVER_NOACK_TIME                                 150
#define CANTRANSCIEVER_PAUSE_TIME                                 150
#define CANTRANSCIEVER_BUSOFF_FAST_CNT                            10
#define CANTRANSCIEVER_BUSOFF_FAST_TIME                           50
#define CANTRANSCIEVER_BUSOFF_SLOW_TIME                           200
/*-------------Sbc port (TCAN4550 transceiver)---------*/
#define LOW  0
#define HIGH 1
#define CanTransciever_SbcInit()                                  TCAN_TransceiverOn()
#define CanTransciever_SbcDeInit()                                TCAN_TransceiverOff()
/*-------------DTC error record stubs------------------*/
#define CanTransciever_SetDtcCurErrFlag(id, flag)                 Rte_SetDtcCurErrFlag(id, flag)
#define CanTransciever_GetDtcDeactiveFlag()                       RteSys_GetDtcDeactiveFlag()
/*-------------Can port (TCAN4550 MCAL)----------------*/
#define CanTransciever_McalInit()                                 TCAN_Init()
#define CanTransciever_McalDeInit()                               TCAN_Disable()
#define CanTransciever_HasEmptyMail()                             (TCAN_GetSendResult() == 0)
#define CanTransciever_ClearMail()                                TCAN_ClearMailbox()
#define CanTransciever_ClearNoAckFlag()                           TCAN_ClearNoAckFlag()
#define CanTransciever_GetNoAckFlag()                             TCAN_GetNoAckFlag()
#define CanTransciever_ClearBusOffFlag()                          TCAN_ClearBusOffFlag()
#define CanTransciever_GetBusOffFlag()                            TCAN_GetBusOffFlag()
#define CanTransciever_ClearBusOffCnt()                           TCAN_ClearBusOffCounter()
#define CanTransciever_GetBusOffCnt()                             TCAN_GetBusOffCounter()
#define CanTransciever_CheckIfTxOK()                              (TCAN_GetSendResult() == 0)
#define CanTransciever_TxMsg(frame, id, p_buff, len)              TCAN_SendAFrame(id, p_buff, len)
/*-------------CanNm port-----------------------------*/
#define CanTransciever_SetTxMsgOKFlag()                           CanNm_SetTxMsgOKFlag()
#define CanTransciever_CheckIfAllowTxMsg()                        CanNM_CheckIfAllowTxMsg()
#define CanTransciever_StartTxNmMsg()                             CanNm_StartTxMsg()
#define CanTransciever_StopTxNmMsg()                              CanNm_StopTxMsg()
/*-------------CanMatrix port-------------------------*/
#define CanTransciever_MatrixTxMsg()                              CanMatrix_TxMsgMain()
#define CanTransciever_CheckIfRxMsg(id)                           CanMatrix_CheckIfRxMsg(id)
/*-------------CanIf port-----------------------------*/
#define CanTransciever_ClearAllTxBuff()                           CanIf_ClearAllRxAndTxBuff()
#define CanTransciever_EnableTxAppUdsMsg()                        CanIF_SwtichAppUdsCtlByNm(FALSE, TRUE)
#define CanTransciever_DisableTxAppUdsMsg()                       CanIF_SwtichAppUdsCtlByNm(FALSE, FALSE)
#define CanTransciever_WriteAppRxMsg(frame, id, p_buff, len)      CanIf_WriteAppRxMsg(frame, id, p_buff, len)
#define CanTransciever_WriteNmRxMsg(frame, id, p_buff, len)       CanIf_WriteNmRxMsg(frame, id, p_buff, len)
#define CanTransciever_WriteUdsRxMsg(frame, id, p_buff, len)      CanIf_WriteUdsRxMsg(frame, id, p_buff, len)
#define CanTransciever_ReadAppTxMsg(p_frame, p_id, p_buff, p_len) CanIf_ReadAppTxMsg(p_frame, p_id, p_buff, p_len)
#define CanTransciever_ReadNmTxMsg(p_frame, p_id, p_buff, p_len)  CanIf_ReadNmTxMsg(p_frame, p_id, p_buff, p_len)
#define CanTransciever_ReadUdsTxMsg(p_frame, p_id, p_buff, p_len) CanIf_ReadUdsTxMsg(p_frame, p_id, p_buff, p_len)
/*-------------enum and struct------------------------*/
typedef enum
{
    CANTRANSCIEVER_NOACK_SEND     = 0x00,
    CANTRANSCIEVER_NOACK_WAIT     = 0x01,
    CANTRANSCIEVER_NOACK_TIME_OUT = 0x02
} CanTransciever_NoAck_TYPE;

typedef enum
{
    CANTRANSCIEVER_BUSOFF_CHECK    = 0x00,
    CANTRANSCIEVER_BUSOFF_QUICK    = 0x01,
    CANTRANSCIEVER_BUSOFF_SLOW     = 0x02,
    CANTRANSCIEVER_BUSOFF_RECOVERY = 0x03
} CanTransciever_BusOff_TYPE;

typedef struct
{
    uint16_t startApp;
    uint16_t startUds;
    uint16_t noAck;
    uint16_t busOff;
} CanTransciever_TimeTick_Type;
#endif
