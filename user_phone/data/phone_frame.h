/***************************************************************************//**
 * @file phone_frame.h
 * @brief V1.1 帧层：明文/加密帧编解码、CRC16 校验。
 *
 * 帧格式参见协议第 5 章和第 22.8 节。
 * CRC16 委托给 user_security_crc16 (CRC-16/CCITT-FALSE)。
 ******************************************************************************/
#ifndef PHONE_FRAME_H
#define PHONE_FRAME_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// 帧解析结果结构体
// =============================================================================

/** 帧头字段 (解码后填充) */
typedef struct {
  uint8_t  magic;
  uint8_t  version;
  uint8_t  msg_type;        /* 0x01=Request, 0x02=Response, 0x03=Event */
  uint8_t  cmd;             /* 命令码 */
  uint16_t seq;             /* 序列号, U16 BE */
  uint8_t  flags;           /* 标志位 */
  uint8_t  frag_index;      /* V1.1 固定 0 */
  uint8_t  frag_total;      /* V1.1 固定 1 */
  uint16_t payload_len;     /* Payload 长度 (加密帧=Ciphertext 长度) */
  const uint8_t *payload;   /* 指向 Payload 起始 (在原始缓冲区中, 不拷贝) */

  /* 加密帧专属字段 */
  bool     is_encrypted;    /* 是否加密帧 */
  uint64_t security_counter;/* SecurityCounter U64 BE (加密帧才有) */
  const uint8_t *auth_tag;  /* 指向 Tag 起始 (加密帧才有, 8 Byte) */

  /* CRC */
  uint16_t crc_recv;        /* 帧中携带的 CRC */
  uint16_t crc_calc;        /* 实际计算的 CRC */

  /* 原始帧信息 */
  uint16_t total_len;       /* 完整帧总长度 */
} phone_frame_t;

// =============================================================================
// 帧编码
// =============================================================================

/**
 * @brief 构造明文帧
 * @param cmd        命令码
 * @param msg_type   消息类型 (Request/Response/Event)
 * @param flags      标志位
 * @param seq        序列号 (U16 BE, Event 固定 0)
 * @param payload    Payload 数据 (可为 NULL 表示空 Payload)
 * @param payload_len Payload 长度
 * @param out_buf    输出缓冲区
 * @param out_len    输出长度
 * @return SL_STATUS_OK 成功, SL_STATUS_INVALID_PARAMETER 缓冲区不足
 */
sl_status_t phone_frame_encode_plain(uint8_t cmd, uint8_t msg_type, uint8_t flags,
                                     uint16_t seq,
                                     const uint8_t *payload, uint16_t payload_len,
                                     uint8_t *out_buf, uint16_t *out_len,
                                     uint16_t buf_capacity);

/**
 * @brief 构造加密帧 (不含加密 —— 调用者需先加密 Payload 再传入)
 *
 * 本函数组装 Header + SecurityCounter + Ciphertext + Tag + CRC。
 * AES-CCM 加密由 phone_crypto 模块完成, 本函数仅做帧层组装。
 *
 * @param cmd              命令码
 * @param msg_type         消息类型
 * @param flags            标志位 (应包含 ENCRYPTED|HAS_AUTH_TAG|LAST_FRAG)
 * @param seq              序列号
 * @param security_counter SecurityCounter (U64 BE)
 * @param ciphertext       AES-CCM 密文 (与明文等长)
 * @param ciphertext_len   密文长度
 * @param auth_tag         AES-CCM Tag (8 Byte)
 * @param out_buf          输出缓冲区
 * @param out_len          输出长度
 * @param buf_capacity     缓冲区容量
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_frame_encode_encrypted(uint8_t cmd, uint8_t msg_type, uint8_t flags,
                                         uint16_t seq,
                                         uint64_t security_counter,
                                         const uint8_t *ciphertext, uint16_t ciphertext_len,
                                         const uint8_t auth_tag[8],
                                         uint8_t *out_buf, uint16_t *out_len,
                                         uint16_t buf_capacity);

// =============================================================================
// 帧解码
// =============================================================================

/**
 * @brief 解码帧头和校验 CRC (不区分明文/加密, 不做解密)
 *
 * 步骤:
 *   1. 校验最小长度 (13 Byte)
 *   2. 校验 Magic (0xA5) 和 Version (0x11)
 *   3. 校验 FragIndex=0, FragTotal=1
 *   4. 根据 Flags 判断是否加密帧, 计算 Payload 起始位置
 *   5. 校验 CRC16
 *
 * 成功返回后, frame->payload 指向 Payload 在原始缓冲区中的位置,
 * 调用者负责进一步处理 (明文 TLV 解析 / 加密 AES-CCM 解密)。
 *
 * @param data   原始帧数据
 * @param len    数据长度
 * @param frame  输出解析结果
 * @return SL_STATUS_OK 成功
 *         SL_STATUS_INVALID_PARAMETER 帧头非法 (Magic/Version/分片)
 *         SL_STATUS_INVALID_SIGNATURE CRC 校验失败
 */
sl_status_t phone_frame_decode(const uint8_t *data, uint16_t len,
                               phone_frame_t *frame);

/**
 * @brief 快速校验帧头合法性 (Magic + Version) 和最小长度
 * @return true 合法
 */
bool phone_frame_is_valid_header(const uint8_t *data, uint16_t len);

// =============================================================================
// CRC16 工具
// =============================================================================

/**
 * @brief 计算 CRC-16/CCITT-FALSE (Poly=0x1021, Init=0xFFFF, RefIn/Out=false, XorOut=0)
 */
uint16_t phone_frame_crc16(const uint8_t *data, uint16_t len);

/**
 * @brief 将 U16 写入 Big Endian 字节
 */
void phone_frame_write_u16_be(uint8_t *buf, uint16_t val);

/**
 * @brief 从 Big Endian 字节读取 U16
 */
uint16_t phone_frame_read_u16_be(const uint8_t *buf);

/**
 * @brief 将 U32 写入 Big Endian 字节
 */
void phone_frame_write_u32_be(uint8_t *buf, uint32_t val);

/**
 * @brief 从 Big Endian 字节读取 U32
 */
uint32_t phone_frame_read_u32_be(const uint8_t *buf);

/**
 * @brief 将 U64 写入 Big Endian 字节
 */
void phone_frame_write_u64_be(uint8_t *buf, uint64_t val);

/**
 * @brief 从 Big Endian 字节读取 U64
 */
uint64_t phone_frame_read_u64_be(const uint8_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_FRAME_H */
