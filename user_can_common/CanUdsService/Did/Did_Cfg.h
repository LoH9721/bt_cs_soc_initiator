/*******************************************************
 * Name    :Did_Cfg.h
 * Function:DID config (BLE adapted, Communi-compatible struct layout)
 *******************************************************/
#ifndef _DID_CFG_H_
#define _DID_CFG_H_
#include "../../Types.h"
#include "../Uds/Uds_Def.h"
#include "../Uds/Uds.h"

#define DID_ADDR_NOE                    0x00

/* Uds port */
#define Did_GetCurrentSessionMode()     Uds_GetCurrentSessionMode()
#define Did_GetCurrentSecurityLevel()   Uds_GetCurrentSecurityLevel()
/* Memory stubs */
#define Did_Copy(p_buff, p_src, len)    do { uint16_t _i; for (_i=0;_i<(len);_i++) ((uint8_t*)(p_buff))[_i]=((const uint8_t*)(p_src))[_i]; } while(0)
#define Did_CheckIfInvalid(p_buff, len) FALSE
#define Did_GetVoltageValue()           (0)
/* NVM stubs */
#define Did_NvmRead(addr, p_buff, len)  FALSE
#define Did_NvmWrite(addr, p_buff, len) FALSE
#define Did_NvmGetProgramStatus()       FALSE

typedef enum { DID_READ_NOE=0x80, DID_READ_LV0=0x00, DID_READ_LV1=0x10, DID_READ_LV_MASK=0x70 } Did_Read_Type;
typedef enum { DID_WRITE_NOE=0x08, DID_WRITE_LV0=0x00, DID_WRITE_LV1=0x01, DID_WRITE_LV_MASK=0x07 } Did_Write_Type;
typedef enum { DID_STORAGE_FLS=0x00, DID_STORAGE_NVM, DID_STORAGE_RAM } Did_Storage_Type;
typedef Bool (*Did_Handle_Fun)(Bool isRead, uint8_t *p_buff, uint8_t len);

typedef struct {
    const uint16_t         did;
    const uint8_t          len;
    const uint8_t          secuAccsLvl;
    const Did_Storage_Type storage;
    const uint32_t         addr;
    const uint8_t         *p_buff;
    const Did_Handle_Fun   dataHandleFun;
} Did_Cfg_Type;

extern Bool Did_CheckIfService0x22ConditionsNotCorrect(uint16_t did);
extern Bool Did_CheckIfService0x2EConditionsNotCorrect(uint16_t did);
extern const uint8_t      g_DidCfgListNum;
extern const Did_Cfg_Type g_DidCfgList[];
#endif
