/*******************************************************
  * Name    :CanIf.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _CANIF_H_
#define _CANIF_H_
/*-------------include file---------------------------*/
#include "../Types.h"
#include "../CanUdsService/Uds/Uds_Def.h"
/*-------------function port--------------------------*/
extern void CanIf_ClearAllRxAndTxBuff(void);
extern void CanIF_SwtichAppUdsCtlByNm(Bool isRxMsg, Bool isEnable);
extern void CanIF_SwtichCtlByUds(Uds_CtrlType_Type ctrlType, Uds_ComType_Type comType);
extern Bool CanIf_WriteAppRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_WriteAppTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_WriteNmRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_WriteNmTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_WriteUdsRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_WriteUdsTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
extern Bool CanIf_ReadAppRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_ReadAppTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_ReadNmRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_ReadNmTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_ReadUdsRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
extern Bool CanIf_ReadUdsTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len);
#endif

