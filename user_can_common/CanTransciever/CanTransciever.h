/*******************************************************
  * Name    :CanTransciever.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _CANTRANSCIEVER_H_
#define _CANTRANSCIEVER_H_
/*-------------include file---------------------------*/
#include "../Types.h"
/*-------------function port--------------------------*/
extern void CanTransciever_Init(void);
extern void CanTransciever_Sleep(void);
extern void CanTransciever_DeInit(void);
extern void CanTransciever_WakeUp(void);
extern void CanTransciever_TimerCtrl(void);
extern void CanTransciever_NormalDeal(void);
extern Bool CanTransciever_CheckStartAppTimerIsOk(void);
extern void CanTransciever_ClearEnableTxAppMsg(void);
extern Bool CanTransciever_RxMsgHandler(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len);
#endif

