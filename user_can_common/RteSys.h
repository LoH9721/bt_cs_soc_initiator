/*******************************************************
 * Name    :RteSys.h
 * Function:RteSys stub for BLE project (replaces Communi RteSys.h)
 *******************************************************/
#ifndef _RTESYS_H_
#define _RTESYS_H_

#include <stdint.h>
#include <stdbool.h>

/* RteSys signal IDs (matching Communi names) */
#define RTESYS_BOOL_DIAG_REQUEST_FLAG    0
#define RTESYS_BOOL_CAN_REASON_FLAG      1
#define RTESYS_BOOL_CAN_VOLTMOD          2

/* RteSys API */
extern void     RteSys_Init(void);         /* configure SysTick 1ms */
extern void     RteSys_Tick1ms(void);
extern uint32_t RteSys_GetSysTimeMs(void);
extern bool     RteSys_GetBoolSig(uint8_t sigId);
extern void     RteSys_SetBoolSig(uint8_t sigId, bool val);
extern uint8_t  RteSys_GetU8Sig(uint8_t sigId);
extern void     RteSys_SetU8Sig(uint8_t sigId, uint8_t val);
extern bool     RteSys_GetLocalSleepFlag(void);
extern void     RteSys_SetLocalSleepFlag(bool flag);
extern bool     RteSys_GetCanSleepFlag(void);
extern void     RteSys_SetCanSleepFlag(bool flag);
extern void     RteSys_SetManualSleepReq(bool flag);
extern bool     RteSys_GetManualSleepReq(void);
extern bool     RteSys_GetDtcDeactiveFlag(void);

#endif /* _RTESYS_H_ */
