/*******************************************************
  * Name    :Dtc_Cfg.h
  * Function:DTC configuration (BLE project - 1 DTC)
  *******************************************************/
#ifndef _DTC_CFG_H_
#define _DTC_CFG_H_
/*-------------include file---------------------------*/
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
#include "../Uds/Uds.h"
/*-------------define---------------------------------*/
#define DTC_CFG_LIST_NUM        1
#define DTC_TEST_FAILED         0x01
#define DTC_CONFIRMED           0x08
#define DTC_SUPPORT_MASK        0x59

/*-------------function port--------------------------*/
#define Dtc_GetCurrentSessionMode()     Uds_GetCurrentSessionMode()
/*-------------enum and struct------------------------*/
typedef enum
{
    DTC_FORMAT_ISO_15031 = 0x00,
    DTC_FORMAT_ISO_14229 = 0x01
} Dtc_Format_Type;

typedef enum
{
    DTC_MODE_INIT = 0x00,
    DTC_MODE_NORMAL
} Dtc_Mode_Type;

typedef struct
{
    uint8_t status;
} Dtc_Data_Type;

typedef struct
{
    uint32_t     groupId;
    uint8_t      supSessMode;
} Dtc_Cfg_Type;
/*-------------variable statement---------------------*/
extern const Dtc_Cfg_Type g_DtcCfgList[DTC_CFG_LIST_NUM];
#endif
