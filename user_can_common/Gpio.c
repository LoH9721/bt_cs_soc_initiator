/*******************************************************
 * Name    :Gpio.c
 * Function:Gpio stub — maps pin output to TCAN4550 transceiver on/off
 *******************************************************/
#include "Gpio.h"

extern void TCAN_TransceiverOn(void);
extern void TCAN_TransceiverOff(void);

void GPIO_SetOutputState(uint32_t pin, uint8_t level)
{
    if (pin == (uint32_t)E_OUTPUT_Can1Enable)
    {
        if (level == LOW)
        {
            TCAN_TransceiverOn();
        }
        else
        {
            TCAN_TransceiverOff();
        }
    }
}
