/*
 * TCAN4x5x_SPI.h — AHB SPI access for TCAN4550 (EFR32 MG24 / EUSART SPI).
 *
 * Replaces MSP430 driverlib usage from TI demo; API matches original TI SPI layer.
 *
 * Copyright (c) 2019 Texas Instruments Incorporated. (Original SPI contract.)
 * SPDX-License-Identifier: BSD-3-Clause
 */

#ifndef TCAN4X5X_SPI_H_
#define TCAN4X5X_SPI_H_

#include <stdint.h>

#define AHB_WRITE_OPCODE                            0x61
#define AHB_READ_OPCODE                             0x41

void AHB_WRITE_32(uint16_t address, uint32_t data);
void AHB_WRITE_BURST_START(uint16_t address, uint8_t words);
void AHB_WRITE_BURST_WRITE(uint32_t data);
void AHB_WRITE_BURST_END(void);

uint32_t AHB_READ_32(uint16_t address);
void AHB_READ_BURST_START(uint16_t address, uint8_t words);
uint32_t AHB_READ_BURST_READ(void);
void AHB_READ_BURST_END(void);

#endif
