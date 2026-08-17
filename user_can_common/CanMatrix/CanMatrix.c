/*******************************************************
  * Name    :CanMatrix.c
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
/*-------------include file---------------------------*/
#include "CanMatrix_Cfg.h"
/*-------------variable statement---------------------*/
static Can_Type                  g_CanMatrixRxMsg;
static CanMatrix_DtcMsgInfo_TYPE g_CanMatrixDtcMsgInfo[CANMATRIX_RX_LIST_NUM];
static CanMatrix_RxMsgInfo_Type  g_CanMatrixRxMsgInfo[CANMATRIX_RX_LIST_NUM];
static CanMatrix_TxMsgInfo_Type  g_CanMatrixTxMsgInfo[CANMATRIX_TX_LIST_NUM];
/*-------------function-------------------------------*/
/*******************************************************
  * Name    :CanMatrix_Init
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanMatrix_Init(void)
{
    uint8_t i = 0;

    CanMatrix_DefaultDataInit();
    for (i = 0; i < CANMATRIX_RX_LIST_NUM; i++)
    {
        g_CanMatrixRxList[i].dataHandleFun(CANMATRIX_RX_STATE_INIT);
    }
}
/*******************************************************
  * Name    :CanMatrix_Init
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2023.12.12
*******************************************************/
void CanMatrix_TxMsgInit(void)
{
    uint8_t i;
    uint32_t now = RteSys_GetSysTimeMs();
    for (i = 0; i < CANMATRIX_TX_LIST_NUM; i++)
    {
        g_CanMatrixTxMsgInfo[i].txTimes      = 0;
        g_CanMatrixTxMsgInfo[i].lastTxTime   = now;
        g_CanMatrixTxMsgInfo[i].lastFastTime = now;
        g_CanMatrixTxMsgInfo[i].txFlag       = FALSE;
    }
}
/*******************************************************
  * Name    :CanMatrix_CheckIfRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanMatrix_CheckIfRxMsg(uint32_t id)
{
    uint8_t i = 0;

    for (i = 0; i < CANMATRIX_RX_LIST_NUM; i++)
    {
        if (g_CanMatrixRxList[i].id == id)
        {
            return TRUE;
        }
    }
    return FALSE;
}

/*******************************************************
  * Name    :CanMatrix_ChechRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_ChechRxMsg(Can_Type *p_msg)
{
    uint8_t i = 0;

    for (i = 0; i < CANMATRIX_RX_LIST_NUM; i++)
    {
        if ((p_msg->frame == g_CanMatrixRxList[i].frame)
            && (p_msg->id == g_CanMatrixRxList[i].id)
            && (p_msg->len >= g_CanMatrixRxList[i].len))
        {
            if (!CanMatrix_Compare(g_CanMatrixRxList[i].p_buff, p_msg->buff, p_msg->len)
                || (g_CanMatrixRxList[i].IsChangeIn == FALSE))
            {
                CanMatrix_Copy(g_CanMatrixRxList[i].p_buff, p_msg->buff, p_msg->len);
                g_CanMatrixRxList[i].dataHandleFun(CANMATRIX_RX_STATE_NORMAL);
            }
            g_CanMatrixRxMsgInfo[i].flag = TRUE;
            break;
        }
    }
    //    if (CanMatrix_CheckIfAllowRecordDTC() == TRUE)
    //    {
    for (i = 0; i < CANMATRIX_DTC_LIST_NUM; i++)
    {
        if (p_msg->id == g_CanMatrixDtcList[i].id)
        {
            g_CanMatrixDtcMsgInfo[i].flag = TRUE;
            break;
        }
    }
    //    }
}

/*******************************************************
  * Name    :CanMatrix_CheckRxIfLost
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_CheckRxIfLost(void)
{
    uint8_t i = 0;

    for (i = 0; i < CANMATRIX_RX_LIST_NUM; i++)
    {
        if (g_CanMatrixRxMsgInfo[i].flag == TRUE)
        {
            g_CanMatrixRxMsgInfo[i].time = 0;
            g_CanMatrixRxMsgInfo[i].flag = FALSE;
            g_CanMatrixRxMsgInfo[i].lost = FALSE;
        }
        else
        {
            if (g_CanMatrixRxMsgInfo[i].lost == TRUE)
            {
                continue;
            }
            else if (g_CanMatrixRxMsgInfo[i].time >= g_CanMatrixRxList[i].lostTime)
            {
                g_CanMatrixRxMsgInfo[i].lost = TRUE;
                g_CanMatrixRxList[i].dataHandleFun(CANMATRIX_RX_STATE_LOST);
            }
            else
            {
                g_CanMatrixRxMsgInfo[i].time += CANMATRIX_RX_SCHEDULE_TIME;
            }
        }
    }
}

/*******************************************************
  * Name    :CanMatrix_CheckDtcIfLost
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_CheckDtcIfLost(void)
{
    uint8_t i = 0;

    for (i = 0; i < CANMATRIX_DTC_LIST_NUM; i++)
    {
        if ((CanMatrix_GetDtcDeactiveFlag() == TRUE)   //&& (CanMatrix_CheckIfAllowRecordDTC() == TRUE)))
            || (CanMatrix_CheckIfAllowRecordDTC() == FALSE)
            || (CanMatrix_CheckNMIfAllowTxMsg() == FALSE)
            || (CanMatrix_CheckIsInNoAck() == TRUE)
            || (CanMatrix_CheckIsInBusOff() == TRUE))
        {
            g_CanMatrixDtcMsgInfo[i].time = 0;
            g_CanMatrixDtcMsgInfo[i].tick = 0;
            g_CanMatrixDtcMsgInfo[i].flag = FALSE;
            if (*g_CanMatrixDtcList[i].lost == TRUE)
            {
                *g_CanMatrixDtcList[i].lost = FALSE;
                g_CanMatrixDtcList[i].dataHandleFun();
            }
            continue;
        }

        if (g_CanMatrixDtcMsgInfo[i].flag == TRUE)
        {
            g_CanMatrixDtcMsgInfo[i].time = 0;
            g_CanMatrixDtcMsgInfo[i].flag = FALSE;
            if (*g_CanMatrixDtcList[i].lost == TRUE)
            {
                if (++g_CanMatrixDtcMsgInfo[i].tick > CANMATRIX_CTR_DTC_TIMES)
                {
                    *g_CanMatrixDtcList[i].lost = FALSE;
                    g_CanMatrixDtcList[i].dataHandleFun();
                }
            }
        }
        else
        {
            if (*g_CanMatrixDtcList[i].lost == TRUE)
            {
                continue;
            }
            else if (g_CanMatrixDtcMsgInfo[i].time >= g_CanMatrixDtcList[i].lostTime)
            {
                g_CanMatrixDtcMsgInfo[i].tick = 0;
                *g_CanMatrixDtcList[i].lost   = TRUE;
                g_CanMatrixDtcList[i].dataHandleFun();
            }
            else
            {
                g_CanMatrixDtcMsgInfo[i].time += CANMATRIX_RX_SCHEDULE_TIME;
            }
        }
    }
}

/*******************************************************
  * Name    :CanMatrix_ClearTxMsgTimer
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_ClearTxMsgTimer(void)
{
    uint8_t i;
    uint32_t now = RteSys_GetSysTimeMs();
    for (i = 0; i < CANMATRIX_TX_LIST_NUM; i++)
    {
        g_CanMatrixTxMsgInfo[i].txTimes      = 0;
        g_CanMatrixTxMsgInfo[i].lastTxTime   = now;
        g_CanMatrixTxMsgInfo[i].lastFastTime = now;
        g_CanMatrixTxMsgInfo[i].txFlag       = FALSE;
    }
}

/*******************************************************
  * Name    :CanMatrix_TxEventMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_TxEventMsg(uint8_t index)
{
    uint8_t                   txTimes = 0;
    CanMatrix_TxList_Type    *p_msg   = NULL;
    CanMatrix_TxMsgInfo_Type *p_info  = NULL;

    p_msg                             = &g_CanMatrixTxList[index];
    p_info                            = &g_CanMatrixTxMsgInfo[index];
    txTimes                           = p_msg->dataHandleFun(CANMATRIX_TX_STATE_CHECK);
    if (txTimes > p_info->txTimes)
    {
        p_info->txTimes = txTimes;
    }
    {
        uint32_t now = RteSys_GetSysTimeMs();
        if ((now - p_info->lastTxTime) >= p_msg->fastPeroid)
        {
            if (p_info->txTimes > 0)
            {
                p_info->txTimes--;
                (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_UPDATE);
                (void)CanMatrix_WriteTxMsg(p_msg->frame, p_msg->id, p_msg->p_buff, p_msg->len);
                if (p_info->txTimes == 0)
                {
                    (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_CLEAR);
                }
                p_info->lastTxTime = now;
            }
        }
    }
}

/*******************************************************
  * Name    :CanMatrix_TxCycleMsg
  * Author  :WangHu
  * Function:周期报文 — 基于 1ms 真实时间差调度 (非阻塞)
  * Version :V1.1.0 (BLE: 替换 timeTick 累加为 RteSys 真实时间)
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_TxCycleMsg(uint8_t index)
{
    CanMatrix_TxList_Type    *p_msg  = NULL;
    CanMatrix_TxMsgInfo_Type *p_info = NULL;

    p_msg  = &g_CanMatrixTxList[index];
    p_info = &g_CanMatrixTxMsgInfo[index];
    (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_CHECK);
    if ((RteSys_GetSysTimeMs() - p_info->lastTxTime) >= p_msg->peroid)
    {
        (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_UPDATE);
        (void)CanMatrix_WriteTxMsg(p_msg->frame, p_msg->id, p_msg->p_buff, p_msg->len);
        (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_CLEAR);
        p_info->lastTxTime = RteSys_GetSysTimeMs();
    }
}

/*******************************************************
  * Name    :CanMatrix_TxCycEvMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_TxCycEvMsg(uint8_t index)
{
    uint8_t                   txTimes = 0;
    CanMatrix_TxList_Type    *p_msg   = NULL;
    CanMatrix_TxMsgInfo_Type *p_info  = NULL;

    p_msg                             = &g_CanMatrixTxList[index];
    p_info                            = &g_CanMatrixTxMsgInfo[index];
    {
        uint32_t now = RteSys_GetSysTimeMs();
        if ((now - p_info->lastTxTime) >= p_msg->peroid)
        {
            p_info->txFlag     = TRUE;
            p_info->lastTxTime = now;
        }
    }
    txTimes = p_msg->dataHandleFun(CANMATRIX_TX_STATE_CHECK);
    if (txTimes > p_info->txTimes)
    {
        p_info->txTimes = txTimes;
    }
    {
        uint32_t now = RteSys_GetSysTimeMs();
        if ((now - p_info->lastFastTime) >= p_msg->fastPeroid)
        {
            if (p_info->txTimes > 0)
            {
                p_info->txTimes--;
                (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_UPDATE);
                (void)CanMatrix_WriteTxMsg(p_msg->frame, p_msg->id, p_msg->p_buff, p_msg->len);
                if (p_info->txTimes == 0)
                {
                    (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_CLEAR);
                }
                p_info->lastFastTime = now;
            }
            else if (p_info->txFlag == TRUE)
            {
                (void)p_msg->dataHandleFun(CANMATRIX_TX_STATE_UPDATE);
                (void)CanMatrix_WriteTxMsg(p_msg->frame, p_msg->id, p_msg->p_buff, p_msg->len);
                p_info->txFlag      = FALSE;
                p_info->lastFastTime = now;
            }
        }
    }
}

/*******************************************************
  * Name    :CanMatrix_TxMsgTask
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanMatrix_TxMsgTask(void)
{
    uint8_t i = 0;

    for (i = 0; i < CANMATRIX_TX_LIST_NUM; i++)
    {
        switch (g_CanMatrixTxList[i].type)
        {
            case EVENT:
                CanMatrix_TxEventMsg(i);
                break;
            case CYCLE:
                CanMatrix_TxCycleMsg(i);
                break;
            case CYCEV:
                CanMatrix_TxCycEvMsg(i);
                break;
            default:
                break;
        }
    }
}

/*******************************************************
  * Name    :CanMatrix_RxMsgMain
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2025.3.15
*******************************************************/
void CanMatrix_RxMsgMain(void)
{
    while (TRUE)
    {
        if (CanMatrix_ReadRxMsg(&g_CanMatrixRxMsg.frame, &g_CanMatrixRxMsg.id, g_CanMatrixRxMsg.buff, &g_CanMatrixRxMsg.len) == TRUE)
        {
            CanMatrix_ChechRxMsg(&g_CanMatrixRxMsg);
        }
        else
        {
            break;
        }
    }
    CanMatrix_CheckRxIfLost();
    CanMatrix_CheckDtcIfLost();
}

/*******************************************************
  * Name    :CanMatrix_TxMsgMain
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanMatrix_TxMsgMain(void)
{
    static CanMatrix_State_Type canMatrixState = CANMATRIX_STATE_INIT;

    switch (canMatrixState)
    {
        case CANMATRIX_STATE_INIT:
            if ((CanMatrix_CheckStartAppTimerIsOk() == TRUE)
                && (CanMatrix_CheckNMIfAllowTxMsg() == TRUE))
            {
                CanMatrix_TxMsgInit();
                canMatrixState = CANMATRIX_STATE_NORMAL;
            }
            break;
        case CANMATRIX_STATE_NORMAL:
            CanMatrix_TxMsgTask();
            if ((CanMatrix_CheckStartAppTimerIsOk() == FALSE)
                || (CanMatrix_CheckNMIfAllowTxMsg() == FALSE))
            {
                canMatrixState = CANMATRIX_STATE_INIT;
            }
            break;
        default:
            canMatrixState = CANMATRIX_STATE_INIT;
            break;
    }
}

