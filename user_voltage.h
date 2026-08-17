/*******************************************************
 * Name    :user_voltage.h
 * Function:电池电压测量模块（IADC 读取 VBAT/4）
*******************************************************/
#ifndef _USER_VOLTAGE_H_
#define _USER_VOLTAGE_H_

#include <stdint.h>
#include <stdbool.h>

void     UserVoltage_Init(void);
void     UserVoltage_MeasureTrigger(void);
uint16_t UserVoltage_GetMV(void);
bool     UserVoltage_IsLowHighStatus(void);

#endif
