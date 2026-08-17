/*******************************************************
  * Name    :CanManage.c
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
/*-------------include file---------------------------*/
#include "CanManage_Cfg.h"
#include "user_log_console.h"
/*-------------variable statement---------------------*/
static CanManage_Mode_Type canManageMode = CANMANAGE_MODE_INIT;  /* 当前管理模式, 供状态显示 */

/*-------------function-------------------------------*/
/*******************************************************
  * Name    :CanManage_TimerCtrl
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanManage_TimerCtrl(void)
{
    CanManage_NmTimerCtrl();
    CanManage_TranscieverTimerCtrl();
}

/*******************************************************
  * Name    :CanManage_Init
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanManage_Main(void)
{
    static uint32_t waitTime = 0;

    switch (canManageMode)
    {
        case CANMANAGE_MODE_INIT:
            if (CanManage_CheckVoltageIfNotOK() == FALSE)
            {
                CanManage_TranscieverInit();
                CanManage_NmStart();
                waitTime      = CanManage_GetSysTimeMs();
                canManageMode = CANMANAGE_MODE_WAIT;
            }
            else
            {
                if (CanManage_CheckLocalIfSleep() == TRUE)
                {
                    CanManage_SetCanSleepFlag(TRUE);
                    canManageMode = CANMANAGE_MODE_SLEEP;
                }
            }
            break;
        case CANMANAGE_MODE_WAIT:
            if ((CanManage_GetSysTimeMs() - waitTime) >= CANMANAGE_ACTIVE_TIME)
            {
                canManageMode = CANMANAGE_MODE_NORMAL;
            }
            break;
        case CANMANAGE_MODE_NORMAL:
        {
            static Bool s_enteredNormal = FALSE;
            if (s_enteredNormal == FALSE)
            {
                s_enteredNormal = TRUE;
                USER_LOG_INFO("[CAN] Manage → NORMAL mode, time=%lu ms" USER_LOG_NL,
                              (unsigned long)CanManage_GetSysTimeMs());
            }
            CanManage_NmMain();
            CanManage_TranscieverNormalDeal();
            if (CanManage_CheckVoltageIfNotOK() == TRUE)
            {
                CanManage_NmStop();
                CanManage_TranscieverDeInit();
                canManageMode = CANMANAGE_MODE_INIT;
            }
            else
            {
                if (CanManage_CheckLocalIfSleep() == TRUE)
                {
                    CanManage_NmSleep();
                    canManageMode = CANMANAGE_MODE_TWBS;
                }
            }
        }
            break;
        case CANMANAGE_MODE_TWBS:
            CanManage_NmMain();
            CanManage_TranscieverNormalDeal();
            if (CanManage_CheckLocalIfSleep() == TRUE)
            {
                if ((CanManage_NmCheckIfSleep() == TRUE) || (CanManage_CheckVoltageIfNotOK() == TRUE))
                {
                    if (CanManage_NmCheckIfSleep() == FALSE)
                    {
                        CanManage_NmSetSleeped();
                    }
                    CanManage_TranscieverSleep();
                    CanManage_SetCanSleepFlag(TRUE);
                    canManageMode = CANMANAGE_MODE_SLEEP;
                }
            }
            else
            {
                CanManage_WakeUp();
                canManageMode = CANMANAGE_MODE_NORMAL;
            }
            break;
        case CANMANAGE_MODE_SLEEP:
            if ((CanManage_CheckLocalIfSleep() == FALSE) || (CanManage_GetCanWakeUpFlag() == TRUE))
            {
                CanManage_SetCanSleepFlag(FALSE);
                CanManage_TranscieverWakeUp();
                CanManage_WakeUp();
                //waitTime      = CanManage_GetSysTimeMs();
                canManageMode = CANMANAGE_MODE_NORMAL;
            }
            break;
        default:
            break;
    }
}

/*******************************************************
  * Name    :CanManage_StubGetTimeMs
  * Function:Bridge for phone_sm — delegates to RteSys
*******************************************************/
uint32_t CanManage_StubGetTimeMs(void)
{
    return CanManage_GetSysTimeMs();
}

/*******************************************************
  * Name    :CanManage_GetMode
  * Function:查询当前管理模式 (供状态显示/调试)
*******************************************************/
CanManage_Mode_Type CanManage_GetMode(void)
{
    return canManageMode;
}

