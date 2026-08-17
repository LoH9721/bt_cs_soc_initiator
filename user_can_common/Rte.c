/*******************************************************
 * Name    :Rte.c
 * Function:Rte stub implementation — DTC no-ops
 *******************************************************/
#include "Rte.h"

bool Rte_GetDtcCurErrFlag(uint32_t dtcId)
{
    (void)dtcId;
    return false;
}

void Rte_SetDtcCurErrFlag(uint32_t dtcId, bool flag)
{
    (void)dtcId;
    (void)flag;
}
