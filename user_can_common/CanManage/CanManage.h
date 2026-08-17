/*******************************************************
  * Name    :CanManage.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _CANMANAGE_H_
#define _CANMANAGE_H_
/*-------------include file---------------------------*/
#include "../Types.h"
#include "CanManage_Cfg.h"   /* CanManage_Mode_Type 枚举 */
/*-------------function port--------------------------*/
extern void     CanManage_Main(void);
extern void     CanManage_TimerCtrl(void);
extern uint32_t CanManage_StubGetTimeMs(void);
extern CanManage_Mode_Type CanManage_GetMode(void);
#endif

