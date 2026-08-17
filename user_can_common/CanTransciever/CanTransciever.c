/*******************************************************
  * Name    :CanTransciever.c
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
/*-------------include file---------------------------*/
#include "CanTransciever_Cfg.h"
#include "user_log_console.h"
/*-------------variable statement---------------------*/
static Can_Type                     g_CanTranscieverTxMsg;
static CanTransciever_TimeTick_Type g_CanTranscieverTimeTick;
static CanTransciever_NoAck_TYPE    g_CanTranscieverNoAckState;
static CanTransciever_BusOff_TYPE   g_CanTranscieverBusOffState;
static Bool                         g_CanTranscieverEnableTxAppMsg;
/*-------------function-------------------------------*/
/*******************************************************
  * Name    :CanTransciever_TimerCtrl
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanTransciever_TimerCtrl(void)
{
    if (g_CanTranscieverEnableTxAppMsg == TRUE)
    {
        if (g_CanTranscieverTimeTick.startApp < 0xFFFF)
        {
            g_CanTranscieverTimeTick.startApp++;
        }
    }

    if (g_CanTranscieverTimeTick.startUds < 0xFFFF)
    {
        g_CanTranscieverTimeTick.startUds++;
    }

    if (g_CanTranscieverTimeTick.noAck < 0xFFFF)
    {
        g_CanTranscieverTimeTick.noAck++;
    }

    if (g_CanTranscieverTimeTick.busOff < 0xFFFF)
    {
        g_CanTranscieverTimeTick.busOff++;
    }
}

/*******************************************************
  * Name    :CanTransciever_Init
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanTransciever_Init(void)
{
    CanTransciever_McalInit();
    CanTransciever_SbcInit();
    g_CanTranscieverNoAckState  = CANTRANSCIEVER_NOACK_SEND;
    g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_CHECK;
}

/*******************************************************
  * Name    :CanTransciever_DeInit
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanTransciever_DeInit(void)
{
    CanTransciever_McalDeInit();
    CanTransciever_SbcDeInit();
}

/*******************************************************
  * Name    :CanTransciever_WakeUp
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanTransciever_WakeUp(void)
{
    CanTransciever_McalInit();
    CanTransciever_SbcInit();
}

/*******************************************************
  * Name    :CanTransciever_Sleep
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanTransciever_Sleep(void)
{
    CanTransciever_ClearMail();
    CanTransciever_ClearNoAckFlag();
    CanTransciever_ClearBusOffCnt();
    CanTransciever_ClearBusOffFlag();
    CanTransciever_McalDeInit();
    CanTransciever_SbcDeInit();
    g_CanTranscieverNoAckState  = CANTRANSCIEVER_NOACK_SEND;
    g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_CHECK;
}

/*******************************************************
  * Name    :CanTransciever_CheckStartAppTimerIsOk
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanTransciever_CheckStartAppTimerIsOk(void)
{
    if ((g_CanTranscieverEnableTxAppMsg == TRUE) && (g_CanTranscieverTimeTick.startApp >= CANTRANSCIEVER_MAX_START_APP_TIME))
    {
        return TRUE;
    }
    return FALSE;
}

/*******************************************************
  * Name    :CanTransciever_DtcErrorRecord
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanTransciever_DtcErrorRecord(void)
{
    if ((CanTransciever_GetDtcDeactiveFlag() == TRUE)
        || (Rte_GetDtcCurErrFlag(DTC_ID_LOW_VOLTAGE) == TRUE)
        || (Rte_GetDtcCurErrFlag(DTC_ID_HIGH_VOLTAGE) == TRUE))
    {
        CanTransciever_SetDtcCurErrFlag(DTC_ID_BUS_OFF, FALSE);
        CanTransciever_ClearBusOffCnt();
        return;
    }

    if (CanTransciever_GetBusOffCnt() >= CANTRANSCIEVER_BUSOFF_DTC_CNT)
    {
        CanTransciever_SetDtcCurErrFlag(DTC_ID_BUS_OFF, TRUE);
    }
    else
    {
        CanTransciever_SetDtcCurErrFlag(DTC_ID_BUS_OFF, FALSE);
    }
}

/*******************************************************
  * Name    :CanTransciever_DtcErrorRecord
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanTransciever_ClearEnableTxAppMsg(void)
{
    g_CanTranscieverEnableTxAppMsg = FALSE;
}

/*******************************************************
  * Name    :CanTransciever_NoAckDeal
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanTransciever_NoAckDeal(void)
{
    switch (g_CanTranscieverNoAckState)
    {
        case CANTRANSCIEVER_NOACK_SEND:
            if (CanTransciever_ReadNmTxMsg(&g_CanTranscieverTxMsg.frame, &g_CanTranscieverTxMsg.id, g_CanTranscieverTxMsg.buff, &g_CanTranscieverTxMsg.len) == TRUE)
            {
                static Bool s_nmTxLogged = FALSE;
                if (s_nmTxLogged == FALSE)
                {
                    s_nmTxLogged = TRUE;
                    USER_LOG_INFO("[CAN] NM TX id=0x%03lX buf=%02X%02X%02X%02X%02X%02X%02X%02X" USER_LOG_NL,
                        (unsigned long)g_CanTranscieverTxMsg.id,
                        (unsigned)g_CanTranscieverTxMsg.buff[0], (unsigned)g_CanTranscieverTxMsg.buff[1],
                        (unsigned)g_CanTranscieverTxMsg.buff[2], (unsigned)g_CanTranscieverTxMsg.buff[3],
                        (unsigned)g_CanTranscieverTxMsg.buff[4], (unsigned)g_CanTranscieverTxMsg.buff[5],
                        (unsigned)g_CanTranscieverTxMsg.buff[6], (unsigned)g_CanTranscieverTxMsg.buff[7]);
                }
                (void)CanTransciever_TxMsg(g_CanTranscieverTxMsg.frame, g_CanTranscieverTxMsg.id, g_CanTranscieverTxMsg.buff, g_CanTranscieverTxMsg.len);
                CanTransciever_ClearNoAckFlag();
                g_CanTranscieverTimeTick.noAck = 0;
                g_CanTranscieverNoAckState     = CANTRANSCIEVER_NOACK_WAIT;
            }
            break;
        case CANTRANSCIEVER_NOACK_WAIT:
            if (CanTransciever_CheckIfTxOK() == TRUE)
            {
                if (g_CanTranscieverEnableTxAppMsg == FALSE)
                {
                    g_CanTranscieverTimeTick.startApp = 0;
                    g_CanTranscieverEnableTxAppMsg    = TRUE;
                }
                CanTransciever_ClearNoAckFlag();
                CanTransciever_ClearBusOffCnt();
                CanTransciever_ClearBusOffFlag();
                CanTransciever_SetTxMsgOKFlag();
                g_CanTranscieverNoAckState = CANTRANSCIEVER_NOACK_SEND;
            }
            else if (g_CanTranscieverTimeTick.startUds <= CANTRANSCIEVER_START_UDS_TIME)
            {
                CanTransciever_ClearBusOffCnt();
                g_CanTranscieverTimeTick.noAck = 0;
            }
            else if ((g_CanTranscieverTimeTick.noAck > 155) || (CanTransciever_GetBusOffCnt() > 0))
            {
                g_CanTranscieverNoAckState = CANTRANSCIEVER_NOACK_SEND;
            }
            else if (CanTransciever_GetNoAckFlag() == TRUE)
            {
                if (g_CanTranscieverTimeTick.noAck >= CANTRANSCIEVER_NOACK_TIME)
                {
                    CanTransciever_ClearMail();
                    CanTransciever_ClearAllTxBuff();
                    CanTransciever_DisableTxAppUdsMsg();
                    g_CanTranscieverTimeTick.noAck = 0;
                    g_CanTranscieverNoAckState     = CANTRANSCIEVER_NOACK_TIME_OUT;
                }
            }
            break;
        case CANTRANSCIEVER_NOACK_TIME_OUT:
            if (g_CanTranscieverTimeTick.noAck >= CANTRANSCIEVER_PAUSE_TIME)
            {
                CanTransciever_Init();
                CanTransciever_ClearNoAckFlag();
                CanTransciever_EnableTxAppUdsMsg();
                if (CanTransciever_CheckIfAllowTxMsg() == TRUE)
                {
                    g_CanTranscieverTimeTick.noAck = 0;
                    (void)CanTransciever_TxMsg(g_CanTranscieverTxMsg.frame, g_CanTranscieverTxMsg.id, g_CanTranscieverTxMsg.buff, g_CanTranscieverTxMsg.len);
                    g_CanTranscieverNoAckState = CANTRANSCIEVER_NOACK_WAIT;
                }
                else
                {
                    g_CanTranscieverNoAckState = CANTRANSCIEVER_NOACK_SEND;
                }
            }
            break;
        default:
            break;
    }
}

/*******************************************************
  * Name    :CanTransciever_BusOffDeal
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanTransciever_BusOffDeal(void)
{
    switch (g_CanTranscieverBusOffState)
    {
        case CANTRANSCIEVER_BUSOFF_CHECK:
            if (g_CanTranscieverTimeTick.startUds > CANTRANSCIEVER_START_UDS_TIME)
            {
                if (CanTransciever_GetBusOffFlag() == TRUE)
                {
                    CanTransciever_ClearAllTxBuff();
                    CanTransciever_DisableTxAppUdsMsg();
                    CanTransciever_StopTxNmMsg();
                    CanTransciever_ClearMail();
                    g_CanTranscieverTimeTick.busOff = 0;
                    if (CANTRANSCIEVER_BUSOFF_FAST_CNT != 0)
                    {
                        g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_QUICK;
                    }
                    else
                    {
                        g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_SLOW;
                    }
                }
            }
            break;
        case CANTRANSCIEVER_BUSOFF_QUICK:
            if (g_CanTranscieverTimeTick.busOff >= CANTRANSCIEVER_BUSOFF_FAST_TIME)
            {
                CanTransciever_Init();
                CanTransciever_ClearBusOffFlag();
                CanTransciever_StartTxNmMsg();
                g_CanTranscieverTimeTick.busOff = 0;
                g_CanTranscieverBusOffState     = CANTRANSCIEVER_BUSOFF_RECOVERY;
            }
            break;
        case CANTRANSCIEVER_BUSOFF_SLOW:
            if (g_CanTranscieverTimeTick.busOff >= CANTRANSCIEVER_BUSOFF_SLOW_TIME)
            {
                g_CanTranscieverTimeTick.busOff = 0;
                CanTransciever_Init();
                CanTransciever_ClearBusOffFlag();
                CanTransciever_StartTxNmMsg();
                g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_RECOVERY;
            }
            break;
        case CANTRANSCIEVER_BUSOFF_RECOVERY:
            if ((g_CanTranscieverTimeTick.busOff >= 100) || (CanTransciever_GetBusOffFlag() == TRUE))
            {
                if (CanTransciever_GetBusOffFlag() == TRUE)
                {
                    CanTransciever_ClearAllTxBuff();
                    CanTransciever_DisableTxAppUdsMsg();
                    CanTransciever_StopTxNmMsg();
                    CanTransciever_ClearMail();
                    g_CanTranscieverTimeTick.busOff = 0;
                    if (CanTransciever_GetBusOffCnt() <= CANTRANSCIEVER_BUSOFF_FAST_CNT)
                    {
                        g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_QUICK;
                    }
                    else
                    {
                        g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_SLOW;
                    }
                }
                else
                {
                    CanTransciever_ClearBusOffCnt();
                    CanTransciever_ClearBusOffFlag();
                    CanTransciever_EnableTxAppUdsMsg();
                    g_CanTranscieverBusOffState = CANTRANSCIEVER_BUSOFF_CHECK;
                }
            }
            break;
        default:
            break;
    }
}

/*******************************************************
  * Name    :CanTransciever_RxTxMsgHandler
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanTransciever_RxMsgHandler(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len)
{
    if ((id == UDS_FUNC_ADDR_ID) || (id == UDS_PHYS_ADDR_ID))
    {
        return CanTransciever_WriteUdsRxMsg(frame, id, p_buff, len);
    }

    if ((id >= CANNM_BASE_ID) && (id <= (CANNM_BASE_ID + 0xFF)) && (id != CANNM_ID))
    {
        return CanTransciever_WriteNmRxMsg(frame, id, p_buff, len);
    }

    if (CanTransciever_CheckIfRxMsg(id) == TRUE)
    {
        return CanTransciever_WriteAppRxMsg(frame, id, p_buff, len);
    }
    return FALSE;
}

/*******************************************************
  * Name    :CanTransciever_RxTxMsgHandler
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanTransciever_TxMsgHandler(void)
{
    uint8_t  i = 0;
    Can_Type msg;

    for (i = 0; i < CANTRANSCIEVER_MAX_TX_MSG_CNT; i++)
    {
        if (CanTransciever_HasEmptyMail() == TRUE)
        {
            if (CanTransciever_ReadAppTxMsg(&msg.frame, &msg.id, msg.buff, &msg.len) == FALSE)
            {
                if (CanTransciever_ReadUdsTxMsg(&msg.frame, &msg.id, msg.buff, &msg.len) == FALSE)
                {
                    break;
                }
            }
            {
                static Bool s_appTxLogged = FALSE;
                if (s_appTxLogged == FALSE)
                {
                    s_appTxLogged = TRUE;
                    USER_LOG_INFO("[CAN] APP TX id=0x%03lX len=%u" USER_LOG_NL,
                        (unsigned long)msg.id, (unsigned)msg.len);
                }
            }
            (void)CanTransciever_TxMsg(msg.frame, msg.id, msg.buff, msg.len);
        }
    }
}

/*******************************************************
  * Name    :CanTransciever_NormalDeal
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanTransciever_NormalDeal(void)
{
    CanTransciever_NoAckDeal();
    CanTransciever_BusOffDeal();
    CanTransciever_MatrixTxMsg();
    CanTransciever_TxMsgHandler();
    CanTransciever_DtcErrorRecord();
}

/*******************************************************
  * Name    :CanTransciever_IsInNoAck
  * Function:返回当前是否处于 NoAck 超时态
  *          (CanIf FIFO 门控需要)
*******************************************************/
Bool CanTransciever_IsInNoAck(void)
{
    return (g_CanTranscieverNoAckState == CANTRANSCIEVER_NOACK_TIME_OUT) ? TRUE : FALSE;
}

/*******************************************************
  * Name    :CanTransciever_IsInBusOff
  * Function:返回当前是否处于 BusOff 恢复态
  *          (CanIf FIFO 门控需要)
*******************************************************/
Bool CanTransciever_IsInBusOff(void)
{
    return (g_CanTranscieverBusOffState != CANTRANSCIEVER_BUSOFF_CHECK) ? TRUE : FALSE;
}

