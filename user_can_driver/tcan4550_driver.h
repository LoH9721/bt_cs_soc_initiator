/***************************************************************************//**
 * @file    tcan4550_driver.h
 * @brief   TCAN4550 芯片驱动内部接口 — 基于 TI TCAN4550 库
 ******************************************************************************/
#ifndef TCAN4550_DRIVER_H_
#define TCAN4550_DRIVER_H_

#include <stdint.h>
#include <stdbool.h>
#include "sl_gpio.h"

/* ===================================================================
 *  GPIO 引脚 (用户硬件确认)
 * =================================================================== */
#define TCAN_PIN_RST_PORT       SL_GPIO_PORT_D
#define TCAN_PIN_RST_PIN        2       // PD02 = CAN_RST
#define TCAN_PIN_NINT_PORT      SL_GPIO_PORT_C
#define TCAN_PIN_NINT_PIN       6       // PC06 = CAN_INT

/* ===================================================================
 *  接收白名单 (SID 过滤器 ID, 用户后续修改)
 * =================================================================== */
#define TCAN_RX_ACCEPT_COUNT    5   /* 4 APP IDs + 1 NM range (0x400-0x4FF) */
extern const uint16_t g_tcan_rx_accept_ids[TCAN_RX_ACCEPT_COUNT];

/* ===================================================================
 *  API
 * =================================================================== */

/// 初始化 GPIO (nCS 在 spi_init 中, RST 和 nINT 在这里)
void tcan4550_gpio_init(void);

/// 硬件复位 TCAN4550
void tcan4550_hw_reset(void);

/// 完整初始化 TCAN4550 (使用 TI 库函数)
/// @param[out] revision 芯片版本号
/// @return true 成功
bool tcan4550_chip_init(uint16_t* revision);

/// 使能 CAN 通信 (通过设备模式控制, MCAN 已在 NORMAL)
void tcan4550_leave_init_mode(void);

/// 进入 INIT 模式
void tcan4550_enter_init_mode(void);

/// 设置设备工作模式
void tcan4550_set_device_mode(uint8_t mode);

/// 读取 MCAN 中断寄存器 (返回中断标志字)
uint32_t tcan4550_read_mcan_ir(void);

/// 清除 MCAN 中断
void tcan4550_clear_mcan_ir(uint32_t ir_flags);

/// 读取设备中断
uint32_t tcan4550_read_dev_ir(void);

/// 清除设备中断
void tcan4550_clear_dev_ir(uint32_t ir_flags);

/// 读 TXBRP
uint32_t tcan4550_read_txbrp(void);

/// 读 TXBTO
uint32_t tcan4550_read_txbto(void);

/// 读 RXF0S
uint32_t tcan4550_read_rxf0s(void);

/// 读 PSR
uint32_t tcan4550_read_psr(void);

/// 读 ECR
uint32_t tcan4550_read_ecr(void);

/// 清除 SPI 错误
void tcan4550_clear_spi_err(void);

/// 从 RX FIFO 0 读取一帧 (TI 库 ReadNextFIFO)
/// @return 读取的字节数, 0 表示 FIFO 空
uint8_t tcan4550_read_next_fifo(uint32_t* out_id, uint8_t* out_data);

/// 写入 TX Buffer 并请求发送 (TI 库 WriteTXBuffer + TransmitBufferContents)
void tcan4550_write_tx_and_send(uint8_t buf_index, uint32_t id,
                                 const uint8_t* data, uint8_t len);

/// 取消所有待发送
void tcan4550_cancel_all_tx(void);

/// 读取并清除 MCAN 中断 (使用 TI 库结构体)
/// @param[out] rf0n  RX FIFO 0 new message
/// @param[out] tc    TX complete
/// @param[out] bo    Bus-Off
/// @param[out] other 其他错误中断
void tcan4550_read_clear_interrupts(bool* rf0n, bool* tc, bool* bo, bool* other);

#endif /* TCAN4550_DRIVER_H_ */
