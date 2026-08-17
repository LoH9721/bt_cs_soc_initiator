/*******************************************************
 * Name    :Can.h
 * Function:Can stub for BLE project — maps to TCAN4550 MCAL
 *******************************************************/
#ifndef _CAN_H_
#define _CAN_H_

#include <stdint.h>
#include <stdbool.h>

extern void     Can_Init(void);
extern void     Can_DeInit(void);
extern bool     Can_HasEmptyMail(void);
extern void     Can_ClearMail(void);
extern void     Can_ClearNoAckFlag(void);
extern bool     Can_GetNoAckFlag(void);
extern void     Can_ClearBusOffFlag(void);
extern bool     Can_GetBusOffFlag(void);
extern void     Can_ClearBusOffCnt(void);
extern uint8_t  Can_GetBusOffCnt(void);
extern bool     Can_CheckIfTxOK(void);
extern void     Can_TxMsg(uint8_t frame, uint32_t id, volatile uint8_t *buf, uint8_t len);

#endif /* _CAN_H_ */
