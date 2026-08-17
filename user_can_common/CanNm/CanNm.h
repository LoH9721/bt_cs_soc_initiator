/*******************************************************
  * Name    :CanNm.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2022.5.20
*******************************************************/
#ifndef _CANNM_H_
#define _CANNM_H_
/*-------------include file---------------------------*/
#include "../Types.h"
#include "CanNm_Cfg.h"   /* CanNm_State_Type 枚举 */
/*-------------function port--------------------------*/
extern void CanNm_Main(void);
extern void CanNm_Stop(void);
extern void CanNm_Sleep(void);
extern void CanNm_Start(void);
extern void CanNm_WakeUp(void);
extern void CanNm_StopTxMsg(void);
extern void CanNm_TimerCtrl(void);
extern void CanNm_SetSleeped(void);
extern void CanNm_StartTxMsg(void);
extern Bool CanNm_CheckIfSleep(void);
extern void CanNm_SetTxMsgOKFlag(void);
extern Bool CanNM_CheckIfAllowTxMsg(void);
extern CanNm_State_Type CanNm_GetState(void);
#endif

