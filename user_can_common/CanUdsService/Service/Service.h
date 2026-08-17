/*******************************************************
  * Name    :Service.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _SERVICE_H_
#define _SERVICE_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
/*-------------function port--------------------------*/
extern Return_Type Service_0x10DiagnosticSessionControl(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x11EcuReset(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x14ClearDiagnosticInformation(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x19ReadDtcInformation(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x22ReadDataByIdentifier(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x23ReadMemoryByAddress(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x27SecurityAccess(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x28CommunicationControl(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x2EWriteDataByIdentifier(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x2FInputOutputControlByIdentifier(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x31RoutineControl(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x3ETesterPresent(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
extern Return_Type Service_0x85ControlDtcSetting(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);
#endif

