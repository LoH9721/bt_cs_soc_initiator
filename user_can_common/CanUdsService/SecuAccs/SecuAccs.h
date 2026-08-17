/*******************************************************
  * Name    :SecuAccs.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _SECUACCS_H_
#define _SECUACCS_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
/*-------------function port--------------------------*/
extern void        SecuAccs_Init(void);
extern void        SecuAccs_TimerCtrl(void);
extern void        SecuAccs_SetSecurityLevel(Uds_DcmSecuLvl_Type secuLvl);
extern Return_Type SecuAccs_0x27SecurityAccess(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
#endif

