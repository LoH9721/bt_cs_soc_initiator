/*******************************************************
  * Name    :Uds.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _UDS_H_
#define _UDS_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
/*-------------function port--------------------------*/
extern void                 Uds_Init(void);
extern void                 Uds_Main(void);
extern void                 Uds_TimerCtrl(void);
extern void                 Uds_ResetS3ServerTime(void);
extern Uds_DcmSessMode_Type Uds_GetCurrentSessionMode(void);
extern Uds_DcmSecuLvl_Type  Uds_GetCurrentSecurityLevel(void);
extern void                 Uds_DslSetSessionMode(Uds_DcmSessMode_Type sessMode);
extern void                 Uds_SetCurrentSecurityLevel(Uds_DcmSecuLvl_Type secuLvl);
#endif

