/***************************************************************************//**
 * @file    tcan4550_spi.c
 * @brief   TCAN4550 SPI 通信层实现
 * @note    使用项目中已初始化的 SPIDRV (EUSART0) 句柄
 *          CS 由软件 GPIO 控制 PB00
 *          每次 AHB 事务打包为单次 SPIDRV_MTransferB 调用
 ******************************************************************************/
#include <string.h>
#include "spidrv.h"
#include "sl_spidrv_instances.h"
#include "sl_gpio.h"
#include "em_gpio.h"
#include "TCAN4x5x_SPI.h"
#include "tcan4550_spi.h"

/* ===================================================================
 *  CS 引脚: PB00 — 软件控制
 * =================================================================== */
#define TCAN_CS_PORT    SL_GPIO_PORT_B
#define TCAN_CS_PIN     0

static void cs_low(void)
{
    sl_gpio_t gpio = { .port = TCAN_CS_PORT, .pin = TCAN_CS_PIN };
    sl_gpio_clear_pin(&gpio);
}

static void cs_high(void)
{
    sl_gpio_t gpio = { .port = TCAN_CS_PORT, .pin = TCAN_CS_PIN };
    sl_gpio_set_pin(&gpio);
}

/* ===================================================================
 *  静态缓冲区
 * =================================================================== */
#define SPI_BUF_MAX_BYTES   512
static uint8_t s_spi_tx_buf[SPI_BUF_MAX_BYTES];
static uint8_t s_spi_rx_buf[SPI_BUF_MAX_BYTES];

/* ===================================================================
 *  初始化
 * =================================================================== */
void tcan4550_spi_init(void)
{
    // 禁用 EUSART0 CS 硬件路由 (Pin Tool 可能将其路由为 CS, 会阻止 GPIO 操作)
    GPIO->EUSARTROUTE[0].ROUTEEN &= ~GPIO_EUSART_ROUTEEN_CSPEN;

    // PB00 → GPIO 推挽输出, 初始高电平 (不选中)
    sl_gpio_t cs = { .port = TCAN_CS_PORT, .pin = TCAN_CS_PIN };
    sl_gpio_set_pin_mode(&cs, SL_GPIO_MODE_PUSH_PULL, 1);
    sl_gpio_set_pin(&cs);
}

/* ===================================================================
 *  单次 32 位写入
 * =================================================================== */
void tcan4550_spi_write32(uint16_t addr, uint32_t data)
{
    cs_low();

    s_spi_tx_buf[0] = AHB_WRITE_OPCODE;
    s_spi_tx_buf[1] = (uint8_t)(addr >> 8);
    s_spi_tx_buf[2] = (uint8_t)(addr & 0xFF);
    s_spi_tx_buf[3] = 1;
    s_spi_tx_buf[4] = (uint8_t)(data >> 24);
    s_spi_tx_buf[5] = (uint8_t)(data >> 16);
    s_spi_tx_buf[6] = (uint8_t)(data >> 8);
    s_spi_tx_buf[7] = (uint8_t)(data & 0xFF);

    SPIDRV_MTransferB(sl_spidrv_eusart_inst_handle, s_spi_tx_buf, s_spi_rx_buf, 8);
    cs_high();
}

/* ===================================================================
 *  单次 32 位读取
 * =================================================================== */
uint32_t tcan4550_spi_read32(uint16_t addr)
{
    memset(s_spi_tx_buf, 0, 8);
    memset(s_spi_rx_buf, 0, 8);

    s_spi_tx_buf[0] = AHB_READ_OPCODE;
    s_spi_tx_buf[1] = (uint8_t)(addr >> 8);
    s_spi_tx_buf[2] = (uint8_t)(addr & 0xFF);
    s_spi_tx_buf[3] = 1;

    cs_low();
    SPIDRV_MTransferB(sl_spidrv_eusart_inst_handle, s_spi_tx_buf, s_spi_rx_buf, 8);
    cs_high();

    return ((uint32_t)s_spi_rx_buf[4] << 24)
         | ((uint32_t)s_spi_rx_buf[5] << 16)
         | ((uint32_t)s_spi_rx_buf[6] << 8)
         |  (uint32_t)s_spi_rx_buf[7];
}

/* ===================================================================
 *  突发写入
 * =================================================================== */
void tcan4550_spi_write_burst(uint16_t addr, const uint32_t* words,
                               uint16_t word_count)
{
    tcan4550_spi_burst_write_start(addr, word_count);
    for (int i = 0; i < word_count; i++) {
        tcan4550_spi_burst_write_word(words[i]);
    }
    tcan4550_spi_burst_write_end();
}

/* ===================================================================
 *  突发写入 START/WORD/END (CS 全程低, 与参考代码时序一致)
 * =================================================================== */
void tcan4550_spi_burst_write_start(uint16_t addr, uint16_t word_count)
{
    cs_low();
    s_spi_tx_buf[0] = AHB_WRITE_OPCODE;
    s_spi_tx_buf[1] = (uint8_t)(addr >> 8);
    s_spi_tx_buf[2] = (uint8_t)(addr & 0xFF);
    s_spi_tx_buf[3] = (uint8_t)(word_count & 0xFF);
    SPIDRV_MTransferB(sl_spidrv_eusart_inst_handle, s_spi_tx_buf, s_spi_rx_buf, 4);
    // CS stays low for subsequent writes
}

void tcan4550_spi_burst_write_word(uint32_t data)
{
    s_spi_tx_buf[0] = (uint8_t)(data >> 24);
    s_spi_tx_buf[1] = (uint8_t)(data >> 16);
    s_spi_tx_buf[2] = (uint8_t)(data >> 8);
    s_spi_tx_buf[3] = (uint8_t)(data);
    SPIDRV_MTransferB(sl_spidrv_eusart_inst_handle, s_spi_tx_buf, s_spi_rx_buf, 4);
}

void tcan4550_spi_burst_write_end(void)
{
    cs_high();
}

/* ===================================================================
 *  突发读取 START/WORD/END
 * =================================================================== */
void tcan4550_spi_burst_read_start(uint16_t addr, uint16_t word_count)
{
    cs_low();
    memset(s_spi_tx_buf, 0, 4);
    s_spi_tx_buf[0] = AHB_READ_OPCODE;
    s_spi_tx_buf[1] = (uint8_t)(addr >> 8);
    s_spi_tx_buf[2] = (uint8_t)(addr & 0xFF);
    s_spi_tx_buf[3] = (uint8_t)(word_count & 0xFF);
    SPIDRV_MTransferB(sl_spidrv_eusart_inst_handle, s_spi_tx_buf, s_spi_rx_buf, 4);
}

uint32_t tcan4550_spi_burst_read_word(void)
{
    memset(s_spi_tx_buf, 0, 4);
    SPIDRV_MTransferB(sl_spidrv_eusart_inst_handle, s_spi_tx_buf, s_spi_rx_buf, 4);
    return ((uint32_t)s_spi_rx_buf[0] << 24) | ((uint32_t)s_spi_rx_buf[1] << 16)
         | ((uint32_t)s_spi_rx_buf[2] << 8)  | (uint32_t)s_spi_rx_buf[3];
}

void tcan4550_spi_burst_read_end(void)
{
    cs_high();
}

/* ===================================================================
 *  突发读取 (CS 全程保持低, 与参考代码时序一致)
 * =================================================================== */
void tcan4550_spi_read_burst(uint16_t addr, uint8_t* out_words,
                              uint16_t word_count)
{
    tcan4550_spi_burst_read_start(addr, word_count);
    for (int i = 0; i < word_count; i++) {
        uint32_t w = tcan4550_spi_burst_read_word();
        int base = i * 4;
        out_words[base + 0] = (uint8_t)(w >> 24);
        out_words[base + 1] = (uint8_t)(w >> 16);
        out_words[base + 2] = (uint8_t)(w >> 8);
        out_words[base + 3] = (uint8_t)(w);
    }
    tcan4550_spi_burst_read_end();
}
