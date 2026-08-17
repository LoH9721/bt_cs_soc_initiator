/*******************************************************
  * Name    :Dtc.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _DTC_H_
#define _DTC_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
/*-------------function port--------------------------*/
extern void        Dtc_Main(void);
extern void        Dtc_ReturnRecord(void);
extern Return_Type Dtc_0x14ClearDiagnosticInformation(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Dtc_0x19ReadDtcInformation(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Dtc_0x85ControlDtcSetting(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
#endif

