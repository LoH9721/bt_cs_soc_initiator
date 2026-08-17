/*******************************************************
 * Name    :Rte.h
 * Function:Rte stub for BLE project — DTC error recording
 *******************************************************/
#ifndef _RTE_H_
#define _RTE_H_

#include <stdbool.h>
#include <stdint.h>

/* DTC IDs used by CanTransciever.c */
#define DTC_ID_LOW_VOLTAGE   0x01U
#define DTC_ID_HIGH_VOLTAGE  0x02U
#define DTC_ID_BUS_OFF       0x03U

extern bool Rte_GetDtcCurErrFlag(uint32_t dtcId);
extern void Rte_SetDtcCurErrFlag(uint32_t dtcId, bool flag);

#endif /* _RTE_H_ */
