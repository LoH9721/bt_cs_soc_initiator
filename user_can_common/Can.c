/*******************************************************
 * Name    :Can.c
 * Function:Can stub implementation — maps all calls to TCAN4550
 *******************************************************/
#include "Can.h"

/* Extern declarations for TCAN4550 driver functions */
extern void     TCAN_Init(void);
extern void     TCAN_Disable(void);
extern bool     TCAN_GetNoAckFlag(void);
extern void     TCAN_ClearNoAckFlag(void);
extern bool     TCAN_GetBusOffFlag(void);
extern void     TCAN_ClearBusOffFlag(void);
extern uint8_t  TCAN_GetBusOffCounter(void);
extern void     TCAN_ClearBusOffCounter(void);
extern int      TCAN_GetSendResult(void);
extern void     TCAN_ClearMailbox(void);
extern void     TCAN_SendAFrame(uint16_t id, const uint8_t *data, uint8_t len);

void Can_Init(void)
{
    TCAN_Init();
}

void Can_DeInit(void)
{
    TCAN_Disable();
}

bool Can_HasEmptyMail(void)
{
    return (TCAN_GetSendResult() == 0);
}

void Can_ClearMail(void)
{
    TCAN_ClearMailbox();
}

void Can_ClearNoAckFlag(void)
{
    TCAN_ClearNoAckFlag();
}

bool Can_GetNoAckFlag(void)
{
    return TCAN_GetNoAckFlag();
}

void Can_ClearBusOffFlag(void)
{
    TCAN_ClearBusOffFlag();
}

bool Can_GetBusOffFlag(void)
{
    return TCAN_GetBusOffFlag();
}

void Can_ClearBusOffCnt(void)
{
    TCAN_ClearBusOffCounter();
}

uint8_t Can_GetBusOffCnt(void)
{
    return TCAN_GetBusOffCounter();
}

bool Can_CheckIfTxOK(void)
{
    return (TCAN_GetSendResult() == 0);
}

void Can_TxMsg(uint8_t frame, uint32_t id, volatile uint8_t *buf, uint8_t len)
{
    (void)frame;
    TCAN_SendAFrame((uint16_t)id, (const uint8_t *)buf, len);
}
