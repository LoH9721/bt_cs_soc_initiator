/***************************************************************************//**
 * @file phone_async_se.c
 * @brief 占位文件 — 异步 SE 方案已废弃, 改用 phone_sm 层延迟处理。
 *
 * 废弃原因: Series 2 (EFR32MG24) SE mailbox 命令格式复杂，
 * sli_se_mailbox_execute_command() 需要正确设置 DMA 描述符链表、
 * keyspec、auth buffer 等参数, 手动构造始终返回 MAILBOX_INVALID (0x70000)。
 *
 * 替代方案: phone_crypto_ecdsa_verify() (PSA → SE, 已验证成功, 82ms),
 * 在 phone_sm 层将 ECDSA 验签延迟到主循环 CAN TX 之后执行。
 *
 * 保留本文件编译占位, 实际功能已移到 phone_sm_deferred_ecdsa_poll()。
 ******************************************************************************/
#include "phone_async_se.h"

bool phone_async_se_is_busy(void)
{
  return false;
}
