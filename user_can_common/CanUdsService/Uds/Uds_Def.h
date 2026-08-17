/*******************************************************
  * Name    :Uds_Def.h
  * Function:UDS base definitions (adapted for BLE project)
 *******************************************************/
#ifndef _UDS_DEF_H_
#define _UDS_DEF_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
/*-------------define---------------------------------*/
#define UDS_FUNC_ADDR_ID            0x7DF
#define UDS_PHYS_ADDR_ID            0x721
#define UDS_RESP_ADDR_ID            0x7A1
#define UDS_RX_BUFF_NB              514
#define UDS_TX_BUFF_NB              300
#define UDS_MAX_NUMBER_OF_BLOCK_LEN UDS_RX_BUFF_NB
#define UDS_FLS_BLOCK_SIZE          (UDS_MAX_NUMBER_OF_BLOCK_LEN - 2)
#define UDS_CRC_TYPE                CRC_16
#define ArraySize(arr)              (sizeof(arr) / sizeof(arr[0]))

/* BLE-specific DTC count */
#define UDS_DTC_CFG_LIST_NUM 1

/*-------------enum and struct------------------------*/
typedef enum
{
    UDS_SESSION_DEFAULT = 0x02,
    UDS_SESSION_EXTEND  = 0x08
} Uds_Session_Type;

typedef enum
{
    UDS_CTR_ENABLE_RX_AND_TX         = 0x00,
    UDS_CTR_ENABLE_RX_AND_DISABLE_TX = 0x01,
    UDS_CTR_DISABLE_RX_AND_ENABLE_TX = 0x02,
    UDS_CTR_DISABLE_RX_AND_TX        = 0x03
} Uds_CtrlType_Type;

typedef enum
{
    UDS_COM_NULL_MSG              = 0x00,
    UDS_COM_NORMAL_MSG            = 0x01,
    UDS_COM_NM_MSG                = 0x02,
    UDS_COM_NORMAL_MSG_AND_NM_MSG = 0x03
} Uds_ComType_Type;

typedef enum
{
    UDS_MODULE_NONE = 0x00,
    UDS_MODULE_DRIVER,
    UDS_MODULE_APP,
    UDS_MODULE_CAL
} Uds_Module_Type;

typedef enum
{
    UDS_DSL_PHY_ADDR = 0x01,
    UDS_DSL_FUN_ADDR
} Uds_DslAddr_Type;

typedef enum
{
    UDS_DCM_DEFAULT_SESSION        = 0x01,
    UDS_DCM_PROGRAMMING_SESSION    = 0x02,
    UDS_DCM_EXTENDED_DIAGN_SESSION = 0x03
} Uds_DcmSessMode_Type;

typedef enum
{
    UDS_DCM_SEC_LEV_NONE = 0x00,
    UDS_DCM_SEC_LEV_1
} Uds_DcmSecuLvl_Type;

typedef enum
{
    UDS_DCM_INITIAL = 0x00,
    UDS_DCM_PENDING,
    UDS_DCM_CANCEL,
    UDS_DCM_FORCE_RCRRP_OK,
    UDS_DCM_POS_RESP_SENT,
    UDS_DCM_POS_RESP_FAILED,
    UDS_DCM_NEG_RESP_SENT,
    UDS_DCM_NEG_RESP_FAILED
} Uds_DcmOpStatus_Type;

typedef enum
{
    UDS_DCM_SID_0X10 = 0x10,
    UDS_DCM_SID_0X11 = 0x11,
    UDS_DCM_SID_0X14 = 0x14,
    UDS_DCM_SID_0X19 = 0x19,
    UDS_DCM_SID_0X22 = 0x22,
    UDS_DCM_SID_0X27 = 0x27,
    UDS_DCM_SID_0X28 = 0x28,
    UDS_DCM_SID_0X2E = 0x2E,
    UDS_DCM_SID_0X31 = 0x31,
    UDS_DCM_SID_0X3E = 0x3E,
    UDS_DCM_SID_0X85 = 0x85
} Uds_DcmSid_Type;

typedef enum
{
    UDS_DCM_E_POS_RESP    = 0x00,
    UDS_DCM_E_NRC_GR      = 0x10,
    UDS_DCM_E_NRC_SNS     = 0x11,
    UDS_DCM_E_NRC_SFNS    = 0x12,
    UDS_DCM_E_NRC_IMLOIF  = 0x13,
    UDS_DCM_E_NRC_RTL     = 0x14,
    UDS_DCM_E_NRC_BRR     = 0x21,
    UDS_DCM_E_NRC_CNC     = 0x22,
    UDS_DCM_E_NRC_RSE     = 0x24,
    UDS_DCM_E_NRC_ROOR    = 0x31,
    UDS_DCM_E_NRC_SAD     = 0x33,
    UDS_DCM_E_NRC_IK      = 0x35,
    UDS_DCM_E_NRC_ENOA    = 0x36,
    UDS_DCM_E_NRC_RTDNE   = 0x37,
    UDS_DCM_E_NRC_UDNA    = 0x70,
    UDS_DCM_E_NRC_TDS     = 0x71,
    UDS_DCM_E_NRC_GPF     = 0x72,
    UDS_DCM_E_NRC_WBSC    = 0x73,
    UDS_DCM_E_NRC_RCRRP   = 0x78,
    UDS_DCM_E_NRC_SFNSIAS = 0x7E,
    UDS_DCM_E_NRC_SNSIAS  = 0x7F,
    UDS_DCM_E_NRC_VTH     = 0x92,
    UDS_DCM_E_NRC_VTL     = 0x93
} Uds_DcmNegRespCode_Type;

typedef struct
{
    Frame_Type       frame;
    uint8_t          sid;
    uint8_t          subFun;
    Bool             supPosResp;
    Uds_DslAddr_Type reqType;
} Uds_DcmProgCon_Type;

typedef void (*Uds_DspStopProcess_Fun)(Uds_DcmOpStatus_Type opStatus);

typedef struct
{
    uint8_t                *p_rxData;
    uint8_t                *p_txData;
    uint16_t                rxLen;
    uint16_t                txLen;
    Bool                    supPosResp;
    uint8_t                 subFun;
    Frame_Type              frame;
    Uds_DslAddr_Type        reqType;
    Uds_DcmNegRespCode_Type finalRespNrc;
    Uds_DspStopProcess_Fun  finalRespToNotify;
} Uds_DcmMsgContext_Type;

typedef struct
{
    const uint16_t funcAddrId;
    const uint16_t physAddrId;
    const uint16_t diagRespId;
    const uint16_t s3ServerMax;
    const uint16_t p2ServerMax;
    const uint16_t p2StartServerMax;
    const uint16_t diagActiveDelay;
} Uds_GenCfg_Type;

/*-------------variable statement---------------------*/
extern const Uds_GenCfg_Type g_UdsGenCfg;
#endif
