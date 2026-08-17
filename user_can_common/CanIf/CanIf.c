/*******************************************************
  * Name    :CanIf.c
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
/*-------------include file---------------------------*/
#include "CanIf_Cfg.h"
/*-------------variable statement---------------------*/
static CanIf_AppRxBuff_Type g_CanIfAppRxBuff;
static CanIf_AppTxBuff_Type g_CanIfAppTxBuff;
static CanIf_NmRxBuff_Type  g_CanIfNmRxBuff;
static CanIf_NmTxBuff_Type  g_CanIfNmTxBuff;
static CanIf_UdsRxBuff_Type g_CanIfUdsRxBuff;
static CanIf_UdsTxBuff_Type g_CanIfUdsTxBuff;
static CanIF_CommCtr_Type   g_CanIFCommCtr = { .data = 0xFF };
/*-------------function-------------------------------*/
/*******************************************************
  * Name    :CanIf_ClearAllRxAndTxBuff
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanIf_ClearAllRxAndTxBuff(void)
{
    g_CanIfAppRxBuff.full       = FALSE;
    g_CanIfAppRxBuff.readindex  = 0;
    g_CanIfAppRxBuff.writeindex = 0;

    g_CanIfAppTxBuff.full       = FALSE;
    g_CanIfAppTxBuff.readindex  = 0;
    g_CanIfAppTxBuff.writeindex = 0;

    g_CanIfNmRxBuff.full        = FALSE;
    g_CanIfNmRxBuff.readindex   = 0;
    g_CanIfNmRxBuff.writeindex  = 0;

    g_CanIfNmTxBuff.full        = FALSE;
    g_CanIfNmTxBuff.readindex   = 0;
    g_CanIfNmTxBuff.writeindex  = 0;

    g_CanIfUdsRxBuff.full       = FALSE;
    g_CanIfUdsRxBuff.readindex  = 0;
    g_CanIfUdsRxBuff.writeindex = 0;

    g_CanIfUdsTxBuff.full       = FALSE;
    g_CanIfUdsTxBuff.readindex  = 0;
    g_CanIfUdsTxBuff.writeindex = 0;
}

/*******************************************************
  * Name    :CanIF_SwtichAppUdsCtlByNm
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanIF_SwtichAppUdsCtlByNm(Bool isRxMsg, Bool isEnable)
{
    if (isRxMsg == TRUE)
    {
        g_CanIFCommCtr.bits.nmEnAppRx = isEnable;
        g_CanIFCommCtr.bits.nmEnUdsRx = isEnable;

        g_CanIfAppRxBuff.full         = FALSE;
        g_CanIfAppRxBuff.readindex    = 0;
        g_CanIfAppRxBuff.writeindex   = 0;

        g_CanIfUdsRxBuff.full         = FALSE;
        g_CanIfUdsRxBuff.readindex    = 0;
        g_CanIfUdsRxBuff.writeindex   = 0;
        return;
    }

    if (isRxMsg == FALSE)
    {
        g_CanIFCommCtr.bits.nmEnAppTx = isEnable;
        g_CanIFCommCtr.bits.nmEnUdsTx = isEnable;

        g_CanIfAppTxBuff.full         = FALSE;
        g_CanIfAppTxBuff.readindex    = 0;
        g_CanIfAppTxBuff.writeindex   = 0;

        g_CanIfUdsTxBuff.full         = FALSE;
        g_CanIfUdsTxBuff.readindex    = 0;
        g_CanIfUdsTxBuff.writeindex   = 0;
        return;
    }
}

/*******************************************************
  * Name    :CanIF_SwtichCtlByUdsRxAndTx
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
static void CanIF_SwtichCtlByUdsRxAndTx(Uds_ComType_Type comType, Bool isRxEnable, Bool isTxEnable)
{
    switch (comType)
    {
        case UDS_COM_NORMAL_MSG:
            g_CanIFCommCtr.bits.udsEnAppRx = isRxEnable;
            g_CanIFCommCtr.bits.udsEnAppTx = isTxEnable;
            break;
        case UDS_COM_NM_MSG:
            g_CanIFCommCtr.bits.udsEnNmRx = isRxEnable;
            g_CanIFCommCtr.bits.udsEnNmTx = isTxEnable;
            break;
        case UDS_COM_NORMAL_MSG_AND_NM_MSG:
            g_CanIFCommCtr.bits.udsEnAppRx = isRxEnable;
            g_CanIFCommCtr.bits.udsEnAppTx = isTxEnable;
            g_CanIFCommCtr.bits.udsEnNmRx  = isRxEnable;
            g_CanIFCommCtr.bits.udsEnNmTx  = isTxEnable;
            break;
        default:
            break;
    }
}

/*******************************************************
  * Name    :CanIF_SwtichCtlByUds
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
void CanIF_SwtichCtlByUds(Uds_CtrlType_Type ctrlType, Uds_ComType_Type comType)
{
    switch (ctrlType)
    {
        case UDS_CTR_ENABLE_RX_AND_TX:
            CanIF_SwtichCtlByUdsRxAndTx(comType, TRUE, TRUE);
            break;
        case UDS_CTR_ENABLE_RX_AND_DISABLE_TX:
            CanIF_SwtichCtlByUdsRxAndTx(comType, TRUE, FALSE);
            break;
        case UDS_CTR_DISABLE_RX_AND_ENABLE_TX:
            CanIF_SwtichCtlByUdsRxAndTx(comType, FALSE, TRUE);
            break;
        case UDS_CTR_DISABLE_RX_AND_TX:
            CanIF_SwtichCtlByUdsRxAndTx(comType, FALSE, FALSE);
            break;
        default:
            break;
    }
}

/*******************************************************
  * Name    :CanIf_WriteAppRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_WriteAppRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len)
{
    if ((g_CanIFCommCtr.bits.nmEnAppRx == TRUE)
        && (g_CanIFCommCtr.bits.udsEnAppRx == TRUE)
        && (g_CanIfAppRxBuff.full == FALSE)
        && (CanIF_CheckIsInNoAck() == FALSE)
        && (CanIF_CheckIsInBusOff() == FALSE))
    {
        g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.writeindex].frame = frame;
        g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.writeindex].id    = id;
        g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.writeindex].len   = len;
        CanIf_Copy(g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.writeindex].buff, p_buff, len);
        if (++g_CanIfAppRxBuff.writeindex >= CANIF_APP_RX_BUFF_SIZE)
        {
            g_CanIfAppRxBuff.writeindex = 0;
        }
        if (g_CanIfAppRxBuff.writeindex == g_CanIfAppRxBuff.readindex)
        {
            g_CanIfAppRxBuff.full = TRUE;
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_ReadAppRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_ReadAppRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len)
{
    if ((g_CanIfAppRxBuff.readindex != g_CanIfAppRxBuff.writeindex) || (g_CanIfAppRxBuff.full == TRUE))
    {
        *p_frame = g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.readindex].frame;
        *p_id    = g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.readindex].id;
        *p_len   = g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.readindex].len;
        CanIf_Copy(p_buff, g_CanIfAppRxBuff.msg[g_CanIfAppRxBuff.readindex].buff, *p_len);
        if (++g_CanIfAppRxBuff.readindex >= CANIF_APP_RX_BUFF_SIZE)
        {
            g_CanIfAppRxBuff.readindex = 0;
        }
        g_CanIfAppRxBuff.full = FALSE;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_WriteAppTxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_WriteAppTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len)
{
    if ((g_CanIFCommCtr.bits.nmEnAppTx == TRUE)
        && (g_CanIFCommCtr.bits.udsEnAppTx == TRUE)
        && (g_CanIfAppTxBuff.full == FALSE)
        && (CanIF_CheckIsInNoAck() == FALSE)
        && (CanIF_CheckIsInBusOff() == FALSE))
    {
        g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.writeindex].frame = frame;
        g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.writeindex].id    = id;
        g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.writeindex].len   = len;
        CanIf_Copy(g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.writeindex].buff, p_buff, len);
        if (++g_CanIfAppTxBuff.writeindex >= CANIF_APP_TX_BUFF_SIZE)
        {
            g_CanIfAppTxBuff.writeindex = 0;
        }
        if (g_CanIfAppTxBuff.writeindex == g_CanIfAppTxBuff.readindex)
        {
            g_CanIfAppTxBuff.full = TRUE;
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_ReadAppTxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_ReadAppTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len)
{
    if ((g_CanIfAppTxBuff.readindex != g_CanIfAppTxBuff.writeindex) || (g_CanIfAppTxBuff.full == TRUE))
    {
        *p_frame = g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.readindex].frame;
        *p_id    = g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.readindex].id;
        *p_len   = g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.readindex].len;
        CanIf_Copy(p_buff, g_CanIfAppTxBuff.msg[g_CanIfAppTxBuff.readindex].buff, *p_len);
        if (++g_CanIfAppTxBuff.readindex >= CANIF_APP_TX_BUFF_SIZE)
        {
            g_CanIfAppTxBuff.readindex = 0;
        }
        g_CanIfAppTxBuff.full = FALSE;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_WriteNmRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_WriteNmRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len)
{
    if ((g_CanIFCommCtr.bits.udsEnNmRx == TRUE) && (g_CanIfNmRxBuff.full == FALSE))
    {
        g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.writeindex].frame = frame;
        g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.writeindex].id    = id;
        g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.writeindex].len   = len;
        CanIf_Copy(g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.writeindex].buff, p_buff, len);
        if (++g_CanIfNmRxBuff.writeindex >= CANIF_NM_RX_BUFF_SIZE)
        {
            g_CanIfNmRxBuff.writeindex = 0;
        }
        if (g_CanIfNmRxBuff.writeindex == g_CanIfNmRxBuff.readindex)
        {
            g_CanIfNmRxBuff.full = TRUE;
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_ReadNmRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_ReadNmRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len)
{
    if ((g_CanIfNmRxBuff.readindex != g_CanIfNmRxBuff.writeindex) || (g_CanIfNmRxBuff.full == TRUE))
    {
        *p_frame = g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.readindex].frame;
        *p_id    = g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.readindex].id;
        *p_len   = g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.readindex].len;
        CanIf_Copy(p_buff, g_CanIfNmRxBuff.msg[g_CanIfNmRxBuff.readindex].buff, *p_len);
        if (++g_CanIfNmRxBuff.readindex >= CANIF_NM_RX_BUFF_SIZE)
        {
            g_CanIfNmRxBuff.readindex = 0;
        }
        g_CanIfNmRxBuff.full = FALSE;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_WriteNmTxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_WriteNmTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len)
{
    if ((g_CanIFCommCtr.bits.udsEnNmTx == TRUE) && (g_CanIfNmTxBuff.full == FALSE))
    {
        g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.writeindex].frame = frame;
        g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.writeindex].id    = id;
        g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.writeindex].len   = len;
        CanIf_Copy(g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.writeindex].buff, p_buff, len);
        if (++g_CanIfNmTxBuff.writeindex >= CANIF_NM_TX_BUFF_SIZE)
        {
            g_CanIfNmTxBuff.writeindex = 0;
        }
        if (g_CanIfNmTxBuff.writeindex == g_CanIfNmTxBuff.readindex)
        {
            g_CanIfNmTxBuff.full = TRUE;
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_ReadNmTxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_ReadNmTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len)
{
    if ((g_CanIfNmTxBuff.readindex != g_CanIfNmTxBuff.writeindex) || (g_CanIfNmTxBuff.full == TRUE))
    {
        *p_frame = g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.readindex].frame;
        *p_id    = g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.readindex].id;
        *p_len   = g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.readindex].len;
        CanIf_Copy(p_buff, g_CanIfNmTxBuff.msg[g_CanIfNmTxBuff.readindex].buff, *p_len);
        if (++g_CanIfNmTxBuff.readindex >= CANIF_NM_TX_BUFF_SIZE)
        {
            g_CanIfNmTxBuff.readindex = 0;
        }
        g_CanIfNmTxBuff.full = FALSE;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_WriteUdsRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_WriteUdsRxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len)
{
    if ((g_CanIFCommCtr.bits.nmEnUdsRx == TRUE)
        && (g_CanIfUdsRxBuff.full == FALSE)
        && (CanIF_CheckIsInNoAck() == FALSE)
        && (CanIF_CheckIsInBusOff() == FALSE))
    {
        g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.writeindex].frame = frame;
        g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.writeindex].id    = id;
        g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.writeindex].len   = len;
        CanIf_Copy(g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.writeindex].buff, p_buff, len);
        if (++g_CanIfUdsRxBuff.writeindex >= CANIF_UDS_RX_BUFF_SIZE)
        {
            g_CanIfUdsRxBuff.writeindex = 0;
        }
        if (g_CanIfUdsRxBuff.writeindex == g_CanIfUdsRxBuff.readindex)
        {
            g_CanIfUdsRxBuff.full = TRUE;
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_ReadUdsRxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_ReadUdsRxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len)
{
    if ((g_CanIfUdsRxBuff.readindex != g_CanIfUdsRxBuff.writeindex) || (g_CanIfUdsRxBuff.full == TRUE))
    {
        *p_frame = g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.readindex].frame;
        *p_id    = g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.readindex].id;
        *p_len   = g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.readindex].len;
        CanIf_Copy(p_buff, g_CanIfUdsRxBuff.msg[g_CanIfUdsRxBuff.readindex].buff, *p_len);
        if (++g_CanIfUdsRxBuff.readindex >= CANIF_UDS_RX_BUFF_SIZE)
        {
            g_CanIfUdsRxBuff.readindex = 0;
        }
        g_CanIfUdsRxBuff.full = FALSE;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_WriteUdsTxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_WriteUdsTxMsg(Frame_Type frame, uint32_t id, volatile uint8_t *p_buff, uint8_t len)
{
    if ((g_CanIFCommCtr.bits.nmEnUdsTx == TRUE)
        && (g_CanIfUdsTxBuff.full == FALSE)
        && (CanIF_CheckIsInNoAck() == FALSE)
        && (CanIF_CheckIsInBusOff() == FALSE))
    {
        g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.writeindex].frame = frame;
        g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.writeindex].id    = id;
        g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.writeindex].len   = len;
        CanIf_Copy(g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.writeindex].buff, p_buff, len);
        if (++g_CanIfUdsTxBuff.writeindex >= CANIF_UDS_TX_BUFF_SIZE)
        {
            g_CanIfUdsTxBuff.writeindex = 0;
        }
        if (g_CanIfUdsTxBuff.writeindex == g_CanIfUdsTxBuff.readindex)
        {
            g_CanIfUdsTxBuff.full = TRUE;
        }
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

/*******************************************************
  * Name    :CanIf_ReadUdsTxMsg
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
Bool CanIf_ReadUdsTxMsg(Frame_Type *p_frame, uint32_t *p_id, volatile uint8_t *p_buff, uint8_t *p_len)
{
    if ((g_CanIfUdsTxBuff.readindex != g_CanIfUdsTxBuff.writeindex) || (g_CanIfUdsTxBuff.full == TRUE))
    {
        *p_frame = g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.readindex].frame;
        *p_id    = g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.readindex].id;
        *p_len   = g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.readindex].len;
        CanIf_Copy(p_buff, g_CanIfUdsTxBuff.msg[g_CanIfUdsTxBuff.readindex].buff, *p_len);
        if (++g_CanIfUdsTxBuff.readindex >= CANIF_UDS_TX_BUFF_SIZE)
        {
            g_CanIfUdsTxBuff.readindex = 0;
        }
        g_CanIfUdsTxBuff.full = FALSE;
        return TRUE;
    }
    else
    {
        return FALSE;
    }
}

