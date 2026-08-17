/***************************************************************************//**
 * @file    TCAN4x5x_SPI.c
 * @brief   AHB SPI 传输层 — 对接 TI TCAN4550 库, 底层调用我们的 SPIDRV
 * @note    所有 CS 操作通过软件 GPIO 控制 PB00
 ******************************************************************************/
#include "TCAN4x5x_SPI.h"
#include "tcan4550_spi.h"

void AHB_WRITE_32(uint16_t address, uint32_t data)
{
    tcan4550_spi_write32(address, data);
}

uint32_t AHB_READ_32(uint16_t address)
{
    return tcan4550_spi_read32(address);
}

void AHB_WRITE_BURST_START(uint16_t address, uint8_t words)
{
    tcan4550_spi_burst_write_start(address, words);
}

void AHB_WRITE_BURST_WRITE(uint32_t data)
{
    tcan4550_spi_burst_write_word(data);
}

void AHB_WRITE_BURST_END(void)
{
    tcan4550_spi_burst_write_end();
}

void AHB_READ_BURST_START(uint16_t address, uint8_t words)
{
    tcan4550_spi_burst_read_start(address, words);
}

uint32_t AHB_READ_BURST_READ(void)
{
    return tcan4550_spi_burst_read_word();
}

void AHB_READ_BURST_END(void)
{
    tcan4550_spi_burst_read_end();
}
