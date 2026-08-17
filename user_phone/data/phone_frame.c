/***************************************************************************//**
 * @file phone_frame.c
 * @brief V1.1 帧层实现：明文/加密帧编解码、CRC16。
 *
 * CRC-16/CCITT-FALSE: Poly=0x1021, Init=0xFFFF, RefIn=false, RefOut=false, XorOut=0
 ******************************************************************************/

#include "user_phone/data/phone_frame.h"
#include "user_phone/phone_cfg.h"
#include <string.h>

// =============================================================================
// CRC16
// =============================================================================

uint16_t phone_frame_crc16(const uint8_t *data, uint16_t len)
{
  uint16_t crc = 0xFFFFU;
  uint16_t i;
  int b;

  if (data == NULL || len == 0U) {
    return crc;
  }
  for (i = 0U; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (b = 0; b < 8; b++) {
      if ((crc & 0x8000U) != 0U) {
        crc = (uint16_t)((crc << 1) ^ 0x1021U);
      } else {
        crc <<= 1;
      }
    }
  }
  return crc;
}

// =============================================================================
// Big Endian 工具
// =============================================================================

void phone_frame_write_u16_be(uint8_t *buf, uint16_t val)
{
  buf[0] = (uint8_t)(val >> 8);
  buf[1] = (uint8_t)(val & 0xFFU);
}

uint16_t phone_frame_read_u16_be(const uint8_t *buf)
{
  return ((uint16_t)buf[0] << 8) | (uint16_t)buf[1];
}

void phone_frame_write_u32_be(uint8_t *buf, uint32_t val)
{
  buf[0] = (uint8_t)(val >> 24);
  buf[1] = (uint8_t)((val >> 16) & 0xFFU);
  buf[2] = (uint8_t)((val >> 8) & 0xFFU);
  buf[3] = (uint8_t)(val & 0xFFU);
}

uint32_t phone_frame_read_u32_be(const uint8_t *buf)
{
  return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16)
       | ((uint32_t)buf[2] << 8)  |  (uint32_t)buf[3];
}

void phone_frame_write_u64_be(uint8_t *buf, uint64_t val)
{
  uint8_t i;
  for (i = 0U; i < 8U; i++) {
    buf[i] = (uint8_t)(val >> (56U - 8U * i));
  }
}

uint64_t phone_frame_read_u64_be(const uint8_t *buf)
{
  uint64_t val = 0ULL;
  uint8_t i;
  for (i = 0U; i < 8U; i++) {
    val = (val << 8) | (uint64_t)buf[i];
  }
  return val;
}

// =============================================================================
// 帧头快速校验
// =============================================================================

bool phone_frame_is_valid_header(const uint8_t *data, uint16_t len)
{
  if (data == NULL || len < PHONE_FRAME_MIN_LEN) {
    return false;
  }
  /* Magic + Version */
  if (data[0] != PHONE_FRAME_MAGIC || data[1] != PHONE_FRAME_VERSION) {
    return false;
  }
  /* FragIndex=0, FragTotal=1 (V1.1 单帧) */
  if (data[7] != 0U || data[8] != 1U) {
    return false;
  }
  return true;
}

// =============================================================================
// 内部: 写帧头
// =============================================================================

static void write_header(uint8_t *buf, uint8_t cmd, uint8_t msg_type, uint8_t flags,
                         uint16_t seq, uint16_t payload_len)
{
  buf[0] = PHONE_FRAME_MAGIC;      /* Magic */
  buf[1] = PHONE_FRAME_VERSION;    /* Version */
  buf[2] = msg_type;               /* MsgType */
  buf[3] = cmd;                    /* Cmd */
  phone_frame_write_u16_be(&buf[4], seq);         /* Seq (U16 BE) */
  buf[6] = flags;                  /* Flags */
  buf[7] = PHONE_FRAG_INDEX;       /* FragIndex=0 */
  buf[8] = PHONE_FRAG_TOTAL;       /* FragTotal=1 */
  phone_frame_write_u16_be(&buf[9], payload_len); /* PayloadLen (U16 BE) */
}

// =============================================================================
// 明文帧编码
// =============================================================================

sl_status_t phone_frame_encode_plain(uint8_t cmd, uint8_t msg_type, uint8_t flags,
                                     uint16_t seq,
                                     const uint8_t *payload, uint16_t payload_len,
                                     uint8_t *out_buf, uint16_t *out_len,
                                     uint16_t buf_capacity)
{
  uint16_t total_len = PHONE_FRAME_HEADER_LEN + payload_len + PHONE_FRAME_CRC_LEN;
  uint16_t crc;

  if (out_buf == NULL || out_len == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (total_len > buf_capacity) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 帧头 */
  write_header(out_buf, cmd, msg_type, flags, seq, payload_len);

  /* Payload */
  if (payload_len > 0U && payload != NULL) {
    memcpy(&out_buf[PHONE_FRAME_HEADER_LEN], payload, payload_len);
  }

  /* CRC16 覆盖 Header + Payload */
  crc = phone_frame_crc16(out_buf, PHONE_FRAME_HEADER_LEN + payload_len);
  phone_frame_write_u16_be(&out_buf[PHONE_FRAME_HEADER_LEN + payload_len], crc);

  *out_len = total_len;
  return SL_STATUS_OK;
}

// =============================================================================
// 加密帧编码 (不含 AES-CCM 加密 — 调用者已完成加密)
// =============================================================================

sl_status_t phone_frame_encode_encrypted(uint8_t cmd, uint8_t msg_type, uint8_t flags,
                                         uint16_t seq,
                                         uint64_t security_counter,
                                         const uint8_t *ciphertext, uint16_t ciphertext_len,
                                         const uint8_t auth_tag[8],
                                         uint8_t *out_buf, uint16_t *out_len,
                                         uint16_t buf_capacity)
{
  /* Header + SecurityCounter(8) + Ciphertext + Tag(8) + CRC(2) */
  uint16_t total_len = PHONE_FRAME_HEADER_LEN + PHONE_SECURITY_COUNTER_LEN
                     + ciphertext_len + PHONE_AES_CCM_TAG_LEN + PHONE_FRAME_CRC_LEN;
  uint16_t crc;
  uint16_t offset = 0U;

  if (out_buf == NULL || out_len == NULL || auth_tag == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (ciphertext_len > 0U && ciphertext == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  if (total_len > buf_capacity) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 帧头 (PayloadLen = Ciphertext 长度) */
  write_header(out_buf, cmd, msg_type, flags, seq, ciphertext_len);

  /* SecurityCounter (U64 BE) */
  phone_frame_write_u64_be(&out_buf[PHONE_FRAME_HEADER_LEN], security_counter);
  offset = PHONE_FRAME_HEADER_LEN + PHONE_SECURITY_COUNTER_LEN;

  /* Ciphertext */
  if (ciphertext_len > 0U) {
    memcpy(&out_buf[offset], ciphertext, ciphertext_len);
    offset += ciphertext_len;
  }

  /* Auth Tag (8 Byte) */
  memcpy(&out_buf[offset], auth_tag, PHONE_AES_CCM_TAG_LEN);
  offset += PHONE_AES_CCM_TAG_LEN;

  /* CRC16 覆盖 Header + SecurityCounter + Ciphertext + Tag */
  crc = phone_frame_crc16(out_buf, offset);
  phone_frame_write_u16_be(&out_buf[offset], crc);
  offset += PHONE_FRAME_CRC_LEN;

  *out_len = total_len;
  return SL_STATUS_OK;
}

// =============================================================================
// 帧解码
// =============================================================================

sl_status_t phone_frame_decode(const uint8_t *data, uint16_t len,
                               phone_frame_t *frame)
{
  uint16_t payload_offset;
  uint16_t crc_offset;
  uint16_t covered_len;
  uint16_t expected_min;

  if (data == NULL || frame == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  memset(frame, 0, sizeof(*frame));

  /* 1. 最小长度校验 */
  if (!phone_frame_is_valid_header(data, len)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 2. 提取帧头字段 */
  frame->magic      = data[0];
  frame->version    = data[1];
  frame->msg_type   = data[2];
  frame->cmd        = data[3];
  frame->seq        = phone_frame_read_u16_be(&data[4]);
  frame->flags      = data[6];
  frame->frag_index = data[7];
  frame->frag_total = data[8];
  frame->payload_len = phone_frame_read_u16_be(&data[9]);

  /* 3. 判断加密帧 */
  frame->is_encrypted = ((frame->flags & PHONE_FLAG_ENCRYPTED) != 0U);

  if (frame->is_encrypted) {
    /* 加密帧: Header(11) + SecurityCounter(8) + Ciphertext(N) + Tag(8) + CRC(2) */
    expected_min = PHONE_FRAME_HEADER_LEN + PHONE_SECURITY_COUNTER_LEN
                 + frame->payload_len + PHONE_AES_CCM_TAG_LEN + PHONE_FRAME_CRC_LEN;
    if (len < expected_min) {
      return SL_STATUS_INVALID_PARAMETER;
    }

    frame->security_counter = phone_frame_read_u64_be(&data[PHONE_FRAME_HEADER_LEN]);
    payload_offset = PHONE_FRAME_HEADER_LEN + PHONE_SECURITY_COUNTER_LEN;
    frame->payload = &data[payload_offset];           /* Ciphertext */
    frame->auth_tag = &data[payload_offset + frame->payload_len]; /* Tag */

    crc_offset = payload_offset + frame->payload_len + PHONE_AES_CCM_TAG_LEN;
    covered_len = crc_offset;
  } else {
    /* 明文帧: Header(11) + Payload(N) + CRC(2) */
    expected_min = PHONE_FRAME_HEADER_LEN + frame->payload_len + PHONE_FRAME_CRC_LEN;
    if (len < expected_min) {
      return SL_STATUS_INVALID_PARAMETER;
    }

    payload_offset = PHONE_FRAME_HEADER_LEN;
    frame->payload = (frame->payload_len > 0U) ? &data[payload_offset] : NULL;
    frame->auth_tag = NULL;

    crc_offset = payload_offset + frame->payload_len;
    covered_len = crc_offset;
  }

  /* 4. CRC 校验 */
  frame->crc_recv = phone_frame_read_u16_be(&data[crc_offset]);
  frame->crc_calc = phone_frame_crc16(data, covered_len);
  if (frame->crc_recv != frame->crc_calc) {
    return SL_STATUS_INVALID_SIGNATURE;  /* CRC 失败 */
  }

  frame->total_len = crc_offset + PHONE_FRAME_CRC_LEN;
  return SL_STATUS_OK;
}
