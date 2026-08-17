/***************************************************************************//**
 * @file phone_async_se.h
 * @brief 真正异步 SE (Secure Engine) crypto 操作
 *
 * 绕过 PSA Crypto / SE Manager 高层同步封装 (sli_se_execute_and_wait),
 * 直接使用 SE mailbox 实现 submit/poll 分离:
 *   1. submit: 设置 SE 命令 → sli_se_mailbox_execute_command() (立即返回)
 *   2. poll:   检查 SEMAILBOX_HOST->RX_STATUS RXINT → 读取结果
 *
 * 在此期间 CPU 可自由运行其他任务 (CAN/BLE 等), 不忙等 SE 完成。
 *
 * 注意: SE 是单通道, 在 poll 到完成前不能提交新的 SE 操作 (由 g_async_busy 保护)。
 ******************************************************************************/
#ifndef PHONE_ASYNC_SE_H
#define PHONE_ASYNC_SE_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/***************************************************************************//**
 * @brief 提交异步 ECDSA P-256 验签 (pre-hashed 模式)
 *
 * @param pub_key  65 字节未压缩公钥 (04||X||Y)
 * @param digest   32 字节 SHA-256 哈希
 * @param sig      64 字节签名 (r||s BE)
 *
 * @return SL_STATUS_OK 命令已提交到 SE mailbox,
 *         SL_STATUS_BUSY 上一次操作尚未完成,
 *         其他错误码表示参数/设置失败
 *
 * @note 调用后应立即返回主循环。之后每轮主循环调用
 *       phone_async_se_poll() 检查是否完成。
 ******************************************************************************/
sl_status_t phone_async_se_submit_ecdsa_verify(const uint8_t pub_key[65],
                                                const uint8_t digest[32],
                                                const uint8_t sig[64]);

/***************************************************************************//**
 * @brief 轮询 SE 是否完成
 *
 * @return SL_STATUS_OK          操作已成功完成 (签名有效)
 *         SL_STATUS_INVALID_SIGNATURE 签名无效 (验签失败)
 *         SL_STATUS_IN_PROGRESS  SE 仍在处理中, 下一轮主循环继续 poll
 *         SL_STATUS_FAIL        操作失败 (SE 内部错误)
 *         SL_STATUS_NOT_READY    没有待处理的操作 (g_async_busy == false)
 ******************************************************************************/
sl_status_t phone_async_se_poll(void);

/***************************************************************************//**
 * @brief 是否正在执行异步加密操作 (SE 忙)
 ******************************************************************************/
bool phone_async_se_is_busy(void);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_ASYNC_SE_H */
