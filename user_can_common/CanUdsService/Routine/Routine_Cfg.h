/*******************************************************
  * Name    :Routine_Cfg.h
  * Function:Routine configuration types (BLE project)
  *******************************************************/
#ifndef _ROUTINE_CFG_H_
#define _ROUTINE_CFG_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
#include "../Uds/Uds.h"
/*-------------define---------------------------------*/
#define ROUTINE_CFG_LIST_NUM              11
/*-------------function port--------------------------*/
#define Routine_GetCurrentSessionMode()   Uds_GetCurrentSessionMode()
#define Routine_GetCurrentSecurityLevel() Uds_GetCurrentSecurityLevel()
/*-------------enum and struct------------------------*/
typedef enum
{
    ROUTINE_CTR_NONE       = 0x00,
    ROUTINE_CTR_START      = 0x01,
    ROUTINE_CTR_STOP       = 0x02,
    ROUTINE_CTR_REQ_RESULT = 0x03
} Routine_Ctr_Type;

typedef enum
{
    ROUTINE_STATE_INIT = 0x00,
    ROUTINE_STATE_STOP = 0x01,
    ROUTINE_STATE_RUN  = 0x02
} Routine_State_Type;

typedef enum
{
    ROUTINE_EXE_INDIRECT = 0x00,
    ROUTINE_EXE_DIRECT   = 0x01
} Routine_Exe_Type;

typedef struct
{
    uint8_t         *p_rxData;
    uint8_t         *p_txData;
    uint16_t         rxLen;
    uint16_t         txLen;
    Routine_Ctr_Type ctrType;
} Routine_MsgContext_Type;

typedef Return_Type (*Routine_Handle_Fun)(Uds_DcmOpStatus_Type opStatus, Routine_MsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);

typedef struct
{
    Routine_State_Type opState;
    Routine_Ctr_Type   ctrType;
} Routine_Info_Type;

typedef struct
{
    const uint16_t            id;
    const uint8_t             len;
    const uint8_t             supSessMode;
    const Uds_DcmSecuLvl_Type secuAccsLvl;
    const Bool                supStop;
    const Bool                supReqResult;
    const Routine_Exe_Type    exeWay;
    const Routine_Handle_Fun  dataHandleFun;
} Routine_Cfg_Type;
/*-------------function statement---------------------*/
/*-------------variable statement---------------------*/
extern const Routine_Cfg_Type g_RoutineCfgList[ROUTINE_CFG_LIST_NUM];
#endif
