/*******************************************************
  * Name    :CanNm.c
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
/*-------------include file---------------------------*/
#include "CanNm_Cfg.h"
/*-------------variable statement---------------------*/
static CanNm_Data_Type     g_CanNmData;
static CanNm_State_Type    g_CanNmState;
static CanNm_TimeCtr_Type  g_CanNmTimeCtr;
static CanNm_TimeTick_Type g_CanNmTimeTick;
static CanNm_RxType_Type   g_CanNmRxType;
static CanNm_TxType_TYPE   g_CanNmTxType;
static Can_Type            g_CanNmRxMsg;
static CanNm_Msg_Type      g_CanNmTxMsg;
/*-------------function statement---------------------*/
/*******************************************************
  * Name    :CanNm_TimerCtrl
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_TimerCtrl(void)
{
    if (g_CanNmTimeCtr.bits.timerout == TRUE)
    {
        if (g_CanNmTimeTick.timerout < 0xFFFF)
        {
            g_CanNmTimeTick.timerout++;
        }
    }

    if (g_CanNmTimeCtr.bits.msgcycle == TRUE)
    {
        if (g_CanNmTimeTick.msgcycle < 0xFFFF)
        {
            g_CanNmTimeTick.msgcycle++;
        }
    }

    if (g_CanNmTimeCtr.bits.repeatmsg == TRUE)
    {
        if (g_CanNmTimeTick.repeatmsg < 0xFFFF)
        {
            g_CanNmTimeTick.repeatmsg++;
        }
    }

    if (g_CanNmTimeCtr.bits.waitbussleep == TRUE)
    {
        if (g_CanNmTimeTick.waitbussleep < 0xFFFF)
        {
            g_CanNmTimeTick.waitbussleep++;
        }
    }

    if (g_CanNmTimeCtr.bits.wakeuptime == TRUE)
    {
        if (g_CanNmTimeTick.wakeuptime < 0xFFFF)
        {
            g_CanNmTimeTick.wakeuptime++;
        }
    }

    if (g_CanNmTimeCtr.bits.diagtime == TRUE)
    {
        if (g_CanNmTimeTick.diagtime < 0xFFFF)
        {
            g_CanNmTimeTick.diagtime++;
        }
    }
}

/*******************************************************
  * Name    :CanNm_EnableRxTxAppUdsMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_EnableRxTxAppUdsMsg(void)
{
    CanNm_SwtichAppUdsCtlMsg(TRUE, TRUE);
    CanNm_SwtichAppUdsCtlMsg(FALSE, TRUE);
}

/*******************************************************
  * Name    :CanNm_DisableRxTxAppUdsMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_DisableRxTxAppUdsMsg(void)
{
    CanNm_SwtichAppUdsCtlMsg(TRUE, FALSE);
    CanNm_SwtichAppUdsCtlMsg(FALSE, FALSE);
}

/*******************************************************
  * Name    :CanNm_PassiveStartUp
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static Bool CanNm_PassiveStartUp(void)
{
    if (g_CanNmData.rxMsgFlag == TRUE
        && ((g_CanNmRxMsg.id & 0xFF) == (g_CanNmRxMsg.buff[0]))
        && (g_CanNmRxMsg.len == 8))
    {
        return TRUE;
    }
    return FALSE;
}

/*******************************************************
  * Name    :CanNm_NetworkRequest
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static Bool CanNm_NetworkRequest(void)
{
    if (g_CanNmData.localRequest == TRUE)
    {
        return TRUE;
    }

    if (g_CanNmState != CANNM_READY_SLEEP_STATE)
    {
        return FALSE;
    }

    if (g_CanNmData.diagRequest == TRUE)
    {
        return TRUE;
    }
    return FALSE;
}

/*******************************************************
  * Name    :CanNm_NetworkRelease
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static Bool CanNm_NetworkRelease(void)
{
    if (g_CanNmData.localRequest == TRUE)
    {
        return FALSE;
    }

    if (g_CanNmData.diagRequest == TRUE)
    {
        return FALSE;
    }
    return TRUE;
}

/*******************************************************
  * Name    :CanNm_RepeatMessageRequest£¨£©
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static Bool CanNm_RepeatMessageRequest(void)
{
    return FALSE;
}

/*******************************************************
  * Name    :CanNm_StateMigrate
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_StateMigrate(CanNm_State_Type state)
{
    g_CanNmState = state;
    switch (g_CanNmState)
    {
        case CANNM_OFF_STATE:
            g_CanNmTimeCtr.bits.timerout     = FALSE;
            g_CanNmTimeCtr.bits.msgcycle     = FALSE;
            g_CanNmTimeCtr.bits.repeatmsg    = FALSE;
            g_CanNmTimeCtr.bits.waitbussleep = FALSE;
            break;
        case CANNM_BUS_SLEEP_MODE:
            g_CanNmTimeCtr.bits.timerout     = FALSE;
            g_CanNmTimeCtr.bits.msgcycle     = FALSE;
            g_CanNmTimeCtr.bits.repeatmsg    = FALSE;
            g_CanNmTimeCtr.bits.waitbussleep = FALSE;
            break;
        case CANNM_REPEAT_MESSAGE_STATE:
            g_CanNmTimeCtr.bits.timerout     = TRUE;
            g_CanNmTimeCtr.bits.msgcycle     = TRUE;
            g_CanNmTimeCtr.bits.repeatmsg    = TRUE;
            g_CanNmTimeCtr.bits.waitbussleep = 0;
            break;
        case CANNM_NORMAL_OPERATION_STATE:
            g_CanNmTimeCtr.bits.timerout     = TRUE;
            g_CanNmTimeCtr.bits.msgcycle     = TRUE;
            g_CanNmTimeCtr.bits.repeatmsg    = FALSE;
            g_CanNmTimeCtr.bits.waitbussleep = FALSE;
            break;
        case CANNM_READY_SLEEP_STATE:
            g_CanNmTimeCtr.bits.timerout     = TRUE;
            g_CanNmTimeCtr.bits.msgcycle     = FALSE;
            g_CanNmTimeCtr.bits.repeatmsg    = FALSE;
            g_CanNmTimeCtr.bits.waitbussleep = FALSE;
            break;
        case CANNM_PREPARE_BUS_SLEEP_MODE:
            g_CanNmTimeCtr.bits.timerout     = FALSE;
            g_CanNmTimeCtr.bits.msgcycle     = FALSE;
            g_CanNmTimeCtr.bits.repeatmsg    = FALSE;
            g_CanNmTimeCtr.bits.waitbussleep = TRUE;
            break;
        default:
            break;
    }
}

/*******************************************************
  * Name    :CanNm_Start
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_Start(void)
{
    g_CanNmData.workIsOn = TRUE;
}

/*******************************************************
  * Name    :CanNm_Stop
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_Stop(void)
{
    g_CanNmData.workIsOn = FALSE;
    CanNm_DisableRxTxAppUdsMsg();
    CanNm_StateMigrate(CANNM_OFF_STATE);
}

/*******************************************************
  * Name    :CanNm_WakeUp
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_WakeUp(void)
{
    if(g_CanNmData.workIsOn == FALSE)
    {
        CanNm_StateMigrate(CANNM_BUS_SLEEP_MODE);
    }

    if (CanNm_GetCanWakeUpFlag() == TRUE)
    {
        CanNm_SetDCanWakeUpFlag(FALSE);
        g_CanNmData.wakeupKeep         = TRUE;
        g_CanNmTimeTick.wakeuptime     = 0;
        g_CanNmTimeCtr.bits.wakeuptime = TRUE;
    }
    else
    {
        g_CanNmData.localRequest = TRUE;
    }
}

/*******************************************************
  * Name    :CanNm_Sleep
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_Sleep(void)
{
    g_CanNmData.localRequest = FALSE;
}

/*******************************************************
  * Name    :CanNm_SetSleeped
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_SetSleeped(void)
{
    CanNm_DisableRxTxAppUdsMsg();
    CanNm_StateMigrate(CANNM_BUS_SLEEP_MODE);
}

/*******************************************************
  * Name    :CanNm_StartTxMsg
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_StartTxMsg(void)
{
    g_CanNmTimeCtr.bits.msgcycle = TRUE;
    g_CanNmTimeTick.msgcycle     = CANNM_T_MESSAGE_CYCLE_TIME;
}

/*******************************************************
  * Name    :CanNm_StopTxMsg
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_StopTxMsg(void)
{
    g_CanNmTimeCtr.bits.msgcycle = FALSE;
    g_CanNmTimeTick.msgcycle     = 0;
}

/*******************************************************
  * Name    :CanNm_SetTxMsgOKFlag
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_SetTxMsgOKFlag(void)
{
    g_CanNmData.txMsgFlag = TRUE;
}

/*******************************************************
  * Name    :CanNM_CheckIfAllowTxMsg
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
Bool CanNM_CheckIfAllowTxMsg(void)
{
    if ((g_CanNmState == CANNM_REPEAT_MESSAGE_STATE)
        || (g_CanNmState == CANNM_NORMAL_OPERATION_STATE)
        || (g_CanNmState == CANNM_READY_SLEEP_STATE))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanNm_CheckIfSleep
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
Bool CanNm_CheckIfSleep(void)
{
    if ((g_CanNmState == CANNM_BUS_SLEEP_MODE)
        && (g_CanNmData.wakeupKeep == FALSE))
    {
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanNm_GetState
  * Function:查询当前 NM 状态 (供状态显示/调试)
*******************************************************/
CanNm_State_Type CanNm_GetState(void)
{
    return g_CanNmState;
}

/*******************************************************
  * Name    :CanNm_RxMsgDisposal
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_RxMsgDisposal(void)
{
    if (CanNm_ReadRxMsg(&g_CanNmRxMsg.frame, &g_CanNmRxMsg.id, g_CanNmRxMsg.buff, &g_CanNmRxMsg.len))
    {
        g_CanNmData.rxMsgFlag = TRUE;
        g_CanNmRxType         = (CanNm_RxType_Type)(g_CanNmRxMsg.buff[1] & 0x11);
    }
    else
    {
        g_CanNmData.rxMsgFlag = FALSE;
        g_CanNmRxType         = CANNM_RX_NULL;
    }

    if (CanNm_GetDiagRequestFlag() == TRUE)
    {
        CanNm_SetDiagRequestFlag(FALSE);
        g_CanNmTimeTick.diagtime     = 0;
        g_CanNmTimeCtr.bits.diagtime = TRUE;
        g_CanNmData.diagRequest      = TRUE;
    }

    if ((g_CanNmData.wakeupKeep == TRUE)
        && (g_CanNmTimeTick.wakeuptime > CANNM_T_WAKEUP_TIMEOUT))
    {
        g_CanNmData.wakeupKeep         = FALSE;
        g_CanNmTimeCtr.bits.wakeuptime = FALSE;
    }

    if ((g_CanNmData.diagRequest == TRUE)
        && (g_CanNmTimeTick.diagtime > CANNM_T_DIAG_TIMEOUT))
    {
        g_CanNmData.diagRequest      = FALSE;
        g_CanNmTimeCtr.bits.diagtime = FALSE;
    }
}

uint8_t g_Test[8];
/*******************************************************
  * Name    :CanNm_TxMsgDisposal
  * Author  :****
  * Function:****
  * Version :V1.0.0
  * Data    :2019.7.30
*******************************************************/
static void CanNm_TxMsgDisposal(void)
{
    g_CanNmTxMsg.data[0]               = (uint8_t)(CANNM_ID & 0xFF);
    g_CanNmTxMsg.data[1]               = (uint8_t)g_CanNmTxType;
    //    g_CanNmTxMsg.bits.reserved32_39    =RteSys_GetU8Sig(RTESYS_U8_DR7808STATEREGIN1);
    //    g_CanNmTxMsg.bits.reserved40_47    =RteSys_GetU8Sig(RTESYS_U8_DR7808STATEREGIN2);
    g_CanNmTxMsg.bits.NFC_PE_RepeatSts = (g_CanNmState == CANNM_REPEAT_MESSAGE_STATE) ? 0x01 : 0x00;

    CanNm_WriteTxMsg(CAN, CANNM_ID, g_CanNmTxMsg.data, 8);
}

/*******************************************************
  * Name    :CanNm_OffStatec
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_OffState(void)
{
    if (g_CanNmData.workIsOn == TRUE)
    {
        if (CanNm_GetCanWakeUpFlag() == TRUE)
        {
            CanNm_SetDCanWakeUpFlag(FALSE);
            g_CanNmData.wakeupKeep         = TRUE;
            g_CanNmTimeTick.wakeuptime     = 0;
            g_CanNmTimeCtr.bits.wakeuptime = TRUE;
        }
        else
        {
            g_CanNmData.localRequest = TRUE;
        }
        CanNm_Fill(g_CanNmTxMsg.data, 0x00, 8);
        CanNm_DisableRxTxAppUdsMsg();
        CanNm_StateMigrate(CANNM_BUS_SLEEP_MODE);
    }
}

/*******************************************************
  * Name    :CanNm_BusSleepMode
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_BusSleepMode(void)
{
    if (CanNm_PassiveStartUp() == TRUE)
    {
        //        g_CanNmTxMsg.bits.NFC_PE_NMReq_NM = 0x01;
        CanNm_EnableRxTxAppUdsMsg();
        g_CanNmTxType              = CANNM_TX_NOT_REPEAT_PASSIVE;
        g_CanNmData.immediateTimes = 0;
        g_CanNmTimeTick.timerout   = 0;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        //        Cannm_EnableTxApp();
        return;
    }

    if (CanNm_NetworkRequest() == TRUE)
    {
        //        g_CanNmTxMsg.bits.NFC_PE_NMReq_DetectNFC = 0x01;
        CanNm_EnableRxTxAppUdsMsg();
        g_CanNmData.wakeupKeep     = FALSE;
        g_CanNmTxType              = CANNM_TX_NOT_REPEAT_ACTIVE;
        g_CanNmData.immediateTimes = CANNM_N_IMMEDIATE_CYCLE_TIMES;
        g_CanNmTimeTick.timerout   = 0;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        //        Cannm_EnableTxApp();
        return;
    }
}

/*******************************************************
  * Name    :CanNm_RepeatMessageState
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_RepeatMessageState(void)
{
    g_CanNmData.wakeupKeep = FALSE;
    if (g_CanNmData.immediateTimes > 0)
    {
        if (g_CanNmTimeTick.msgcycle >= CANNM_T_IMMEDIATE_CYCLE_TIME)
        {
            g_CanNmData.immediateTimes--;
            g_CanNmTimeTick.msgcycle = 0;
            CanNm_TxMsgDisposal();
        }
    }
    else
    {
        if (g_CanNmTimeTick.msgcycle >= CANNM_T_MESSAGE_CYCLE_TIME)
        {
            g_CanNmTimeTick.msgcycle = 0;
            CanNm_TxMsgDisposal();
        }
    }
    if (g_CanNmTimeTick.timerout >= CANNM_T_TIMEOUT_TIME)
    {
        g_CanNmTimeTick.timerout = 0;
    }

    if ((g_CanNmTimeTick.repeatmsg >= CANNM_T_REPEAT_MESSAGE_TIME)
        && (CanNm_NetworkRequest() == TRUE))
    {
        g_CanNmTxType = (CanNm_TxType_TYPE)(g_CanNmTxType & CANNM_TX_NOT_REPEAT_ACTIVE);
        CanNm_StateMigrate(CANNM_NORMAL_OPERATION_STATE);
        return;
    }

    if ((g_CanNmTimeTick.repeatmsg >= CANNM_T_REPEAT_MESSAGE_TIME)
        && (CanNm_NetworkRelease() == TRUE))
    {
        g_CanNmTxType = (CanNm_TxType_TYPE)(g_CanNmTxType & CANNM_TX_NOT_REPEAT_ACTIVE);
        CanNm_StateMigrate(CANNM_READY_SLEEP_STATE);
        return;
    }
}

/*******************************************************
  * Name    :CanNm_NormalOperationState
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_NormalOperationState(void)
{
    if (g_CanNmTimeTick.msgcycle >= CANNM_T_MESSAGE_CYCLE_TIME)
    {
        g_CanNmTimeTick.msgcycle = 0;
        CanNm_TxMsgDisposal();
    }
    if (g_CanNmTimeTick.timerout >= CANNM_T_TIMEOUT_TIME)
    {
        g_CanNmTimeTick.timerout = 0;
    }

    if ((g_CanNmData.rxMsgFlag == TRUE)
        && ((g_CanNmRxType == CANNM_RX_REPEAT_PASSIVE) || (g_CanNmRxType == CANNM_RX_REPEAT_ACTIVE)))
    {
        g_CanNmData.immediateTimes = 0;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        return;
    }

    if (CanNm_RepeatMessageRequest() == TRUE)
    {
        g_CanNmTxType              = (CanNm_TxType_TYPE)(g_CanNmTxType | CANNM_TX_REPEAT_PASSIVE);
        g_CanNmData.immediateTimes = CANNM_N_IMMEDIATE_CYCLE_TIMES;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        return;
    }

    if (CanNm_NetworkRelease() == TRUE)
    {
        CanNm_StateMigrate(CANNM_READY_SLEEP_STATE);
        return;
    }
}

/*******************************************************
  * Name    :CanNm_ReadySleepState
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_ReadySleepState(void)
{
    if (g_CanNmTimeTick.timerout >= CANNM_T_TIMEOUT_TIME)
    {
        g_CanNmTimeTick.waitbussleep = 0;
        CanNm_DisableRxTxAppUdsMsg();
        CanNm_StateMigrate(CANNM_PREPARE_BUS_SLEEP_MODE);
        return;
    }

    if ((g_CanNmData.rxMsgFlag == TRUE)
        && ((g_CanNmRxType == CANNM_RX_REPEAT_PASSIVE) || (g_CanNmRxType == CANNM_RX_REPEAT_ACTIVE)))
    {
        g_CanNmData.immediateTimes = 0;
        g_CanNmTimeTick.timerout   = 0;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        return;
    }

    if (CanNm_RepeatMessageRequest() == TRUE)
    {
        g_CanNmTxType              = (CanNm_TxType_TYPE)(g_CanNmTxType | CANNM_TX_REPEAT_PASSIVE);
        g_CanNmData.immediateTimes = CANNM_N_IMMEDIATE_CYCLE_TIMES;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        return;
    }

    if (CanNm_NetworkRequest() == TRUE)
    {
        g_CanNmTimeTick.timerout = 0;
        g_CanNmTimeTick.msgcycle = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_NORMAL_OPERATION_STATE);
        return;
    }
}

/*******************************************************
  * Name    :CanNm_PrepareBusSleepMode
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_PrepareBusSleepMode(void)
{
    if (g_CanNmTimeTick.waitbussleep >= CANNM_T_WAIT_BUS_SLEEP_TIME)
    {
        CanNm_DisableRxTxAppUdsMsg();
        CanNm_StateMigrate(CANNM_BUS_SLEEP_MODE);
        Cannm_ClearEnableTxApp();
        CanNm_ClearRxAndTxBuff();
        return;
    }

    if (CanNm_NetworkRequest() == TRUE)
    {
        CanNm_EnableRxTxAppUdsMsg();
        g_CanNmTxType              = CANNM_TX_NOT_REPEAT_ACTIVE;
        g_CanNmData.immediateTimes = CANNM_N_IMMEDIATE_CYCLE_TIMES;
        g_CanNmTimeTick.timerout   = 0;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        return;
    }

    if (CanNm_PassiveStartUp() == TRUE)
    {
        CanNm_EnableRxTxAppUdsMsg();
        g_CanNmTxType              = CANNM_TX_NOT_REPEAT_PASSIVE;
        g_CanNmData.immediateTimes = 0;
        g_CanNmTimeTick.timerout   = 0;
        g_CanNmTimeTick.repeatmsg  = 0;
        g_CanNmTimeTick.msgcycle   = CANNM_T_MESSAGE_CYCLE_TIME;
        CanNm_StateMigrate(CANNM_REPEAT_MESSAGE_STATE);
        return;
    }
}

/*******************************************************
  * Name    :CanNm_StateJumpMain
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_StateJumpMain(void)
{
    switch (g_CanNmState)
    {
        case CANNM_OFF_STATE:
            CanNm_OffState();
            break;
        case CANNM_BUS_SLEEP_MODE:
            CanNm_BusSleepMode();
            break;
        case CANNM_REPEAT_MESSAGE_STATE:
            CanNm_RepeatMessageState();
            break;
        case CANNM_NORMAL_OPERATION_STATE:
            CanNm_NormalOperationState();
            break;
        case CANNM_READY_SLEEP_STATE:
            CanNm_ReadySleepState();
            break;
        case CANNM_PREPARE_BUS_SLEEP_MODE:
            CanNm_PrepareBusSleepMode();
            break;
        default:
            break;
    }
}

/*******************************************************
  * Name    :CanNm_ClearTimerout
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
static void CanNm_ClearTimerout(void)
{
    if ((g_CanNmState == CANNM_REPEAT_MESSAGE_STATE)
        || (g_CanNmState == CANNM_NORMAL_OPERATION_STATE)
        || (g_CanNmState == CANNM_READY_SLEEP_STATE))
    {
        if ((g_CanNmData.rxMsgFlag == TRUE) || (g_CanNmData.txMsgFlag == TRUE))
        {
            g_CanNmTimeTick.timerout = 0;
            g_CanNmData.rxMsgFlag    = FALSE;
            g_CanNmData.txMsgFlag    = FALSE;
        }
    }
}

/*******************************************************
  * Name    :CanNm_Main
  * Author  :WangHu
  * Function:***
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanNm_Main(void)
{
    CanNm_RxMsgDisposal();
    CanNm_StateJumpMain();
    CanNm_ClearTimerout();
}

