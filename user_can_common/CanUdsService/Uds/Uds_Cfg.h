/*******************************************************
  * Name    :Uds_Cfg.h
  * Function:UDS configuration (adapted for BLE project)
 *******************************************************/
#ifndef _UDS_CFG_H_
#define _UDS_CFG_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "Uds_Def.h"
#include "../Service/Service.h"
#include "../Routine/Routine.h"
#include "../SecuAccs/SecuAccs.h"
#include "../../CanTp/CanTp.h"
#include "../../CanIf/CanIf.h"
#include "../IoCtr/IoCtr.h"
#include "../Dtc/Dtc.h"
/*-------------define---------------------------------*/
#define UDS_DCM_DSL_RX_BUFF_SIZE                     UDS_RX_BUFF_NB
#define UDS_DCM_DSL_TX_BUFF_SIZE                     UDS_TX_BUFF_NB
#define UDS_DCM_SUBFUNCTION_VALUE_MASK               0x7F
#define UDS_DCM_SUPP_RESSPOS_BIT_MASK                0x80
#define UDS_DCM_DIAG_RESP_MAX_NUM                    5
/*-------------function port--------------------------*/
/* UDS timer tick values S3=5000, P2=50, P2*=4000 */
#define UDS_S3_SERVER_MAX           5000
#define UDS_P2_SERVER_MAX           50
#define UDS_P2_START_SERVER_MAX     4000

/*-------------CanTp port-----------------------------*/
#define Uds_RxUdsTpMsg(p_frame, p_id, p_buff, p_len) CanTp_UdsRxMsg(p_frame, p_id, p_buff, p_len)
#define Uds_TxUdsTpMsg(frame, id, p_buff, len)       CanTp_UdsTxMsg(frame, id, p_buff, len)
/*-------------Routine port---------------------------*/
#define Uds_RoutineInit()                            Routine_Init()
/*-------------SecuAccs port--------------------------*/
#define Uds_SecuAccsInit()                           SecuAccs_Init()
#define Uds_SecuAccsTimerCtrl()                      SecuAccs_TimerCtrl()
#define Uds_SetSecurityLevel(secuLvl)                SecuAccs_SetSecurityLevel(secuLvl)
/*-------------IoCtr port-----------------------------*/
#define Uds_IoCtrInit()                              IoCtr_Init()
#define Uds_IoCtrTimerCtrl()                         IoCtr_TimerCtrl()
#define Uds_IoReturnControlToEcu()                   IoCtr_ReturnControlToEcu()
/*-------------Dtc port-------------------------------*/
#define Uds_DtcMainFunction()                        Dtc_Main()
#define Uds_DtcReturnRecord()                        Dtc_ReturnRecord()
/*-------------Memory stub----------------------------*/
#define Uds_Copy(des, src, len)                      do { uint16_t _i; for (_i = 0; _i < (len); _i++) { ((uint8_t*)(des))[_i] = ((uint8_t*)(src))[_i]; } } while(0)
/*-------------CanIf port-----------------------------*/
#define Uds_SwtichCtlByDefaultSession()              CanIF_SwtichCtlByUds(UDS_CTR_ENABLE_RX_AND_TX, UDS_COM_NORMAL_MSG_AND_NM_MSG)

/*-------------enum and struct------------------------*/
typedef enum
{
    UDS_SUPPORT_PHY_ADDR = 0x02,
    UDS_SUPPORT_FUN_ADDR = 0x04
} Uds_Support_Type;

typedef enum
{
    UDS_DSP_RESP_IDLE       = 0x00,
    UDS_DSP_RESP_REQUESTED  = 0x01,
    UDS_DSP_PUT_NO_RESP     = 0x02,
    UDS_DSP_PUT_POS_RESP    = 0x04,
    UDS_DSP_PUT_NEG_RESP    = 0x08,
    UDS_DSP_WAIT_FINAL_RESP = 0x10,
    UDS_DSP_JUMP_DIAG_RESP  = 0x20,
    UDS_DSP_REPEAT_RESP     = 0x80
} Uds_DspDiagState_Type;

typedef enum
{
    UDS_MODE_RULE_DEFAULT = 0x00,
    UDS_MODE_RULE_1
} Uds_modeRule_Type;

typedef enum
{
    UDS_DCM_PROG_COND_CHK_WAIT = 0x00,
    UDS_DCM_PROG_COND_CHK_IGNORE,
    UDS_DCM_PROG_COND_CHK_FINISH
} Uds_DcmProgConCheck_Type;

typedef Bool        (*Uds_DcmModeRuleCheck_Fun)(Uds_DcmNegRespCode_Type *p_errCode);
typedef Return_Type (*Uds_DsdServHandle_Fun)(Uds_DcmOpStatus_Type opStatus, Uds_DcmMsgContext_Type *p_msgContext, Uds_DcmNegRespCode_Type *p_errCode);

typedef struct
{
    uint16_t diagActive;
    uint16_t s3Server;
    uint16_t p2Server;
} Uds_TimeTick_Type;

typedef struct
{
    const uint8_t             sid;
    const uint8_t             supSessMode;
    const Uds_DcmSecuLvl_Type secuAccsLvl;
    const Uds_modeRule_Type   modeRule;
} Uds_DcmSubServ_Type;

typedef struct
{
    const uint8_t               sid;
    const uint8_t               supSessMode;
    const uint8_t               supAddrWay;
    const Uds_DcmSecuLvl_Type   secuAccsLvl;
    const Uds_modeRule_Type     modeRule;
    const Bool                  supPosResp;
    const uint8_t               subFunNum;
    const Uds_DcmSubServ_Type  *subServRef;
    const Uds_DsdServHandle_Fun servHandleFun;
} Uds_DcmServCfg_Type;

typedef struct
{
    uint8_t                    sid;
    uint8_t                    pendCnt;
    Uds_DspDiagState_Type      diagState;
    Uds_DcmOpStatus_Type       opStatus;
    Uds_DcmSessMode_Type       curSessMode;
    Uds_DcmSecuLvl_Type        curSecuLvl;
    Uds_DcmMsgContext_Type     context;
    Uds_DsdServHandle_Fun      servHandleFun;
    const Uds_DcmServCfg_Type *mainServCfgRef;
    Uds_DcmProgConCheck_Type   progConCheck;
} Uds_Status_Type;

typedef struct
{
    Frame_Type frame;
    uint32_t   id;
    uint16_t   len;
    uint8_t    sdu[UDS_DCM_DSL_RX_BUFF_SIZE];
    uint8_t    data[UDS_DCM_DSL_RX_BUFF_SIZE];
} Uds_DslRxPdu_Type;

typedef struct
{
    Frame_Type frame;
    uint32_t   id;
    uint16_t   len;
    uint8_t    sdu[UDS_DCM_DSL_TX_BUFF_SIZE];
} Uds_DslTxPdu_Type;
/*-------------variable statement---------------------*/
extern const uint8_t                  g_UdsServiceCfgListNum;
extern const uint8_t                  g_UdsModeRuleFunListNum;
extern const Uds_DcmServCfg_Type      g_UdsServiceCfgList[];
extern const Uds_DcmModeRuleCheck_Fun g_UdsModeRuleFunList[];
#endif
