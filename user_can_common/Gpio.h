/*******************************************************
 * Name    :Gpio.h
 * Function:Gpio stub for BLE project — maps to TCAN4550 transceiver control
 *******************************************************/
#ifndef _GPIO_H_
#define _GPIO_H_

#include <stdint.h>

typedef enum { E_OUTPUT_Can1Enable = 0 } GpioOutput;

#define LOW  0
#define HIGH 1

extern void GPIO_SetOutputState(uint32_t pin, uint8_t level);

#endif /* _GPIO_H_ */
