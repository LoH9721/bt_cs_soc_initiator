/*******************************************************
  * Name    :CanManage_Cfg.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _CANMANAGE_CFG_H_
#define _CANMANAGE_CFG_H_
/*-------------include file---------------------------*/
#include "../Types.h"
#include "../RteSys.h"
#include "../BatteryCtr.h"
#include "../user_can_config.h"
/*-------------extern declarations---------------------*/
extern void CanNm_Main(void);
extern void CanNm_Stop(void);
extern void CanNm_Sleep(void);
extern void CanNm_Start(void);
extern void CanNm_WakeUp(void);
extern void CanNm_TimerCtrl(void);
extern void CanNm_SetSleeped(void);
extern Bool CanNm_CheckIfSleep(void);
extern void CanTransciever_Init(void);
extern void CanTransciever_Sleep(void);
extern void CanTransciever_DeInit(void);
extern void CanTransciever_WakeUp(void);
extern void CanTransciever_TimerCtrl(void);
extern void CanTransciever_NormalDeal(void);
#if CAN_UDS_ENABLE
extern void CanTp_TimerCtrl(void);
extern void CanTp_Main(void);
extern void Uds_TimerCtrl(void);
extern void Uds_Main(void);
#endif
/*-------------define---------------------------------*/
#define CANMANAGE_ACTIVE_TIME             25
/*-------------function port--------------------------*/
/*-------------BatteryCtr port------------------------*/
#define CanManage_CheckVoltageIfNotOK()   BatteryCtr_IsInLowHighStatus()
/*-------------CanTransciever port--------------------*/
#if CAN_UDS_ENABLE
#define CanManage_TranscieverTimerCtrl()  do { CanTransciever_TimerCtrl(); CanTp_TimerCtrl(); Uds_TimerCtrl(); } while(0)
#define CanManage_TranscieverNormalDeal() do { CanTransciever_NormalDeal(); CanTp_Main(); Uds_Main(); } while(0)
#else
#define CanManage_TranscieverTimerCtrl()  CanTransciever_TimerCtrl()
#define CanManage_TranscieverNormalDeal() CanTransciever_NormalDeal()
#endif
#define CanManage_TranscieverInit()       CanTransciever_Init()
#define CanManage_TranscieverDeInit()     CanTransciever_DeInit()
#define CanManage_TranscieverWakeUp()     CanTransciever_WakeUp()
#define CanManage_TranscieverSleep()      CanTransciever_Sleep()
/*-------------CanNm port-----------------------------*/
#define CanManage_NmTimerCtrl()           CanNm_TimerCtrl()
#define CanManage_NmMain()                CanNm_Main()
#define CanManage_NmStart()               CanNm_Start()
#define CanManage_NmStop()                CanNm_Stop()
#define CanManage_WakeUp()                CanNm_WakeUp()
#define CanManage_NmSleep()               CanNm_Sleep()
#define CanManage_NmCheckIfSleep()        CanNm_CheckIfSleep()
#define CanManage_NmSetSleeped()          CanNm_SetSleeped()
/*-------------RteSys port----------------------------*/
#define CanManage_GetSysTimeMs()          RteSys_GetSysTimeMs()
#define CanManage_CheckLocalIfSleep()     RteSys_GetLocalSleepFlag()
#define CanManage_SetCanSleepFlag(flag)   RteSys_SetCanSleepFlag(flag)
#define CanManage_GetCanWakeUpFlag()      RteSys_GetBoolSig(RTESYS_BOOL_CAN_REASON_FLAG)
/*-------------enum and struct------------------------*/
typedef enum
{
    CANMANAGE_MODE_INIT = 0x00,
    CANMANAGE_MODE_WAIT,
    CANMANAGE_MODE_NORMAL,
    CANMANAGE_MODE_TWBS,
    CANMANAGE_MODE_SLEEP
} CanManage_Mode_Type;
#endif
