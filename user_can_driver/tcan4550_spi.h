/***************************************************************************//**
 * @file    tcan4550_spi.h
 * @brief   TCAN4550 SPI 通信层 (AHB 协议封装)
 * @note    使用现有 SPIDRV (EUSART0), CS 由硬件 Block 模式自动控制
 *          每次 AHB 事务打包为一次 SPIDRV_MTransferB 调用
 ******************************************************************************/
#ifndef TCAN4550_SPI_H_
#define TCAN4550_SPI_H_

#include <stdint.h>

/* ---- 初始化 ---- */

/// 初始化 SPI 通信层（配置 EUSART0 硬件 CS 到 PB00）
void tcan4550_spi_init(void);

/* ---- 单次 32 位读写 ---- */

/// 向 TCAN4550 寄存器写入 32 位值
void     tcan4550_spi_write32(uint16_t addr, uint32_t data);

/// 从 TCAN4550 寄存器读取 32 位值
uint32_t tcan4550_spi_read32(uint16_t addr);

/* ---- 突发读写 ---- */

/// 突发写入：将 word_count 个 32 位字写入 MRAM
/// 等价于: START + N×WRITE + END, CS 全程保持低电平
/// @param addr   MRAM 起始地址
/// @param words  数据缓冲区 (uint32_t 数组, 内部自动 MSB-first)
/// @param word_count  字数
void     tcan4550_spi_write_burst(uint16_t addr, const uint32_t* words,
                                   uint16_t word_count);

/// 突发写入 START: 发送命令头, CS 拉低 (后续 WRITE/END 操作 CS 保持低)
void     tcan4550_spi_burst_write_start(uint16_t addr, uint16_t word_count);

/// 突发写入一个 32 位字 (在 START 和 END 之间调用, CS 保持低)
void     tcan4550_spi_burst_write_word(uint32_t data);

/// 突发写入 END: 拉高 CS, 完成事务
void     tcan4550_spi_burst_write_end(void);

/// 突发读取 START: 发送读命令, CS 拉低
void     tcan4550_spi_burst_read_start(uint16_t addr, uint16_t word_count);

/// 突发读取一个 32 位字 (在 START 和 END 之间调用)
uint32_t tcan4550_spi_burst_read_word(void);

/// 突发读取 END: 拉高 CS
void     tcan4550_spi_burst_read_end(void);

/// 突发读取：从 MRAM 读取 word_count 个 32 位字 (兼容接口)
void     tcan4550_spi_read_burst(uint16_t addr, uint8_t* out_words,
                                  uint16_t word_count);

#endif /* TCAN4550_SPI_H_ */
