/*******************************************************
  * Name    :Routine.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _ROUTINE_H_
#define _ROUTINE_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
/*-------------function port--------------------------*/
extern void        Routine_Init(void);
extern Return_Type Routine_0x31RoutineControl(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
#endif

