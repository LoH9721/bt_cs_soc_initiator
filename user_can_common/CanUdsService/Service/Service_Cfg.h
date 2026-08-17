/*******************************************************
  * Name    :Service_Cfg.h
  * Function:Service config/bridge macros (BLE project)
 *******************************************************/
#ifndef _SERVICE_CFG_H_
#define _SERVICE_CFG_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
#include "../Uds/Uds.h"
#include "../Did/Did.h"
#include "../Dtc/Dtc.h"
#include "../../CanIf/CanIf.h"
#include "../Routine/Routine.h"
#include "../SecuAccs/SecuAccs.h"
/*-------------function port--------------------------*/
/* Uds port */
#define Service_GetCurrentSessionMode()        Uds_GetCurrentSessionMode()
#define Service_ResetS3ServerTime()            Uds_ResetS3ServerTime()
#define Service_DslSetSessionMode(sessMode)    Uds_DslSetSessionMode(sessMode)
/* Did port */
#define Did_0x22Read(opStatus, p_msg, p_err)   Did_0x22ReadDataByIdentifier(opStatus, p_msg, p_err)
#define Did_0x2EWrite(opStatus, p_msg, p_err)  Did_0x2EWriteDataByIdentifier(opStatus, p_msg, p_err)
/* Routine port */
#define Routine_0x31Control(opStatus, p_msg, p_err)  Routine_0x31RoutineControl(opStatus, p_msg, p_err)
/* SecuAccs port */
#define SecuAccs_0x27Access(opStatus, p_msg, p_err)  SecuAccs_0x27SecurityAccess(opStatus, p_msg, p_err)
/* Dtc port */
#define Dtc_0x14Clear(opStatus, p_msg, p_err)   Dtc_0x14ClearDiagnosticInformation(opStatus, p_msg, p_err)
#define Dtc_0x19Read(opStatus, p_msg, p_err)    Dtc_0x19ReadDtcInformation(opStatus, p_msg, p_err)
#define Dtc_0x85Control(opStatus, p_msg, p_err) Dtc_0x85ControlDtcSetting(opStatus, p_msg, p_err)
/* CanIf port */
#define CanIF_SwtichCtlByUds(ctrlType, comType) CanIF_SwtichCtlByUds(ctrlType, comType)
#endif
