/*******************************************************
  * Name    :CanMatrix.h
  * Author  :WangHu
  * Function:****
  * Version :V1.0.0
  * Data    :2024.1.2
*******************************************************/
#ifndef _CANMATRIX_H_
#define _CANMATRIX_H_
/*-------------include file---------------------------*/
#include "../Types.h"
/*-------------function port--------------------------*/
extern void CanMatrix_Init(void);
extern void CanMatrix_RxMsgMain(void);
extern void CanMatrix_TxMsgMain(void);
extern Bool CanMatrix_CheckIfRxMsg(uint32_t id);
#endif

