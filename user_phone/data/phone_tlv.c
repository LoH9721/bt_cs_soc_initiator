/***************************************************************************//**
 * @file phone_tlv.c
 * @brief V1.1 TLV 编解码实现。
 ******************************************************************************/

#include "user_phone/data/phone_tlv.h"
#include <string.h>
#include <stdio.h>

/* TLV Header: Type(1) + Length(2 BE) = 3 bytes */
#define TLV_HEADER_LEN  3U

/* ==================== 辅助函数 ==================== */

static void write_u16_be(uint8_t *buf, uint16_t val)
{
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

static uint16_t read_u16_be(const uint8_t *buf)
{
    return ((uint16_t)buf[0] << 8) | buf[1];
}

static uint32_t read_u32_be(const uint8_t *buf)
{
    return ((uint32_t)buf[0] << 24) | ((uint32_t)buf[1] << 16) |
           ((uint32_t)buf[2] << 8) | buf[3];
}

static uint64_t read_u64_be(const uint8_t *buf)
{
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val = (val << 8) | buf[i];
    }
    return val;
}

/* ==================== TLV 校验实现 ==================== */

sl_status_t phone_tlv_validate_required(const phone_tlv_t *tlvs, uint8_t count,
                                         const uint8_t *required_types, uint8_t req_count)
{
    if (tlvs == NULL || required_types == NULL) {
        return SL_STATUS_INVALID_PARAMETER;
    }
    
    for (uint8_t i = 0; i < req_count; i++) {
        if (phone_tlv_get(tlvs, count, required_types[i]) == NULL) {
            return SL_STATUS_INVALID_PARAMETER;
        }
    }
    return SL_STATUS_OK;
}

bool phone_tlv_validate_length(uint8_t type, uint16_t len)
{
    switch (type) {
        case PHONE_TLV_TOKEN_BODY:          return len <= PHONE_TLV_MAX_TOKEN_BODY;
        case PHONE_TLV_SIG:                 return len == PHONE_TLV_LEN_SIG;
        case PHONE_TLV_DEVICE_ID:           return len > 0 && len <= PHONE_TLV_MAX_DEVICE_ID;
        case PHONE_TLV_APP_PUBLIC_KEY:      return len == PHONE_TLV_LEN_PUBKEY;
        case PHONE_TLV_NONCE:               return len == PHONE_TLV_LEN_NONCE;
        case PHONE_TLV_APP_SIGNATURE:       return len == PHONE_TLV_LEN_SIG;
        case PHONE_TLV_CMD_PARAM:           return len <= PHONE_TLV_MAX_CMD_PARAM;
        case PHONE_TLV_APP_KEY_ID:          return len == PHONE_TLV_LEN_KEY_ID;
        case PHONE_TLV_AUTH_SESSION_ID:     return len == PHONE_TLV_LEN_SESSION_ID;
        case PHONE_TLV_APP_COUNTER:         return len == PHONE_TLV_LEN_APP_COUNTER;
        case PHONE_TLV_BIND_STATE:          return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_PROTOCOL_VERSION:    return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_REMAIN_SEC:          return len == PHONE_TLV_LEN_U16;
        case PHONE_TLV_ERROR_CODE:          return len == PHONE_TLV_LEN_U16;
        case PHONE_TLV_RESULT:              return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_QID:                 return len > 0 && len <= PHONE_TLV_MAX_QID;
        case PHONE_TLV_CV:                  return len == PHONE_TLV_LEN_U32;
        case PHONE_TLV_CONTROL_CMD:         return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_CAPABILITY_FLAGS:    return len == PHONE_TLV_LEN_CAPABILITY_FLAGS;
        case PHONE_TLV_BIND_VERSION:        return len == PHONE_TLV_LEN_BIND_VERSION;
        case PHONE_TLV_CHALLENGE_ID:        return len == PHONE_TLV_LEN_CHALLENGE_ID;
        case PHONE_TLV_VEHICLE_LOCK_STATE:  return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_APP_ECDH_PUBLIC_KEY: return len == PHONE_TLV_LEN_PUBKEY;
        case PHONE_TLV_BG24_ECDH_PUBLIC_KEY:return len == PHONE_TLV_LEN_PUBKEY;
        case PHONE_TLV_PUBLIC_KEY_ALG:      return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_CONTROL_FLAGS:       return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_APP_BIND_NONCE:      return len == PHONE_TLV_LEN_NONCE;
        case PHONE_TLV_BG_BIND_NONCE:       return len == PHONE_TLV_LEN_NONCE;
        case PHONE_TLV_BIND_SESSION_ID:     return len == PHONE_TLV_LEN_SESSION_ID;
        case PHONE_TLV_FW_VERSION:          return len == PHONE_TLV_LEN_FW_VERSION;
        case PHONE_TLV_REQUEST_SEQ:         return len == PHONE_TLV_LEN_U16;
        /* V1.2 新增 */
        case PHONE_TLV_IGNITION_STATE:      return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_REMAINING_RANGE:     return len == PHONE_TLV_LEN_U16;
        case PHONE_TLV_DOOR_STATE:          return len == PHONE_TLV_LEN_U8;
        case PHONE_TLV_PAIRING_WINDOW_ID:   return len == PHONE_TLV_LEN_PAIRING_WINDOW_ID;
        case PHONE_TLV_PAIRING_PASSKEY:     return len == PHONE_TLV_LEN_PAIRING_PASSKEY;
        case PHONE_TLV_PASSIVE_SENSITIVITY: return len == PHONE_TLV_LEN_U8;
        default:
            return true;
    }
}

bool phone_tlv_has_duplicates(const phone_tlv_t *tlvs, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        for (uint8_t j = i + 1; j < count; j++) {
            if (tlvs[i].type == tlvs[j].type) {
                return true;
            }
        }
    }
    return false;
}

/* ==================== 新 API 实现 (供 phone_sm.c 使用) ==================== */

/**
 * @brief TLV 编码实现
 */
sl_status_t phone_tlv_encode(const phone_tlv_t *tlvs, uint8_t tlv_count,
                             uint8_t *out_buf, uint16_t *out_len,
                             uint16_t buf_cap)
{
    if (tlvs == NULL || out_buf == NULL || out_len == NULL) {
        return SL_STATUS_INVALID_PARAMETER;
    }
    
    uint16_t offset = 0;
    
    for (uint8_t i = 0; i < tlv_count; i++) {
        /* 检查缓冲区空间: Type(1) + Length(2) + Value(len) */
        uint16_t needed = TLV_HEADER_LEN + tlvs[i].len;
        if (offset + needed > buf_cap) {
            return SL_STATUS_NO_MORE_RESOURCE;
        }
        
        /* 写入 Type */
        out_buf[offset++] = tlvs[i].type;
        
        /* 写入 Length (Big Endian) */
        write_u16_be(&out_buf[offset], tlvs[i].len);
        offset += 2;
        
        /* 写入 Value */
        if (tlvs[i].len > 0 && tlvs[i].value != NULL) {
            memcpy(&out_buf[offset], tlvs[i].value, tlvs[i].len);
        }
        offset += tlvs[i].len;
    }
    
    *out_len = offset;
    return SL_STATUS_OK;
}

/**
 * @brief TLV 解析实现
 */
sl_status_t phone_tlv_parse(const uint8_t *data, uint16_t data_len,
                            phone_tlv_t *out_tlvs, uint8_t *out_count,
                            uint8_t max_count)
{
    if (data == NULL || out_tlvs == NULL || out_count == NULL) {
        return SL_STATUS_INVALID_PARAMETER;
    }
    
    uint8_t count = 0;
    uint16_t offset = 0;
    
    while (offset < data_len) {
        /* 检查是否有足够的空间读取 Header */
        if (data_len - offset < TLV_HEADER_LEN) {
            return SL_STATUS_INVALID_PARAMETER;
        }
        
        if (count >= max_count) {
            return SL_STATUS_NO_MORE_RESOURCE;
        }
        
        /* 读取 Type */
        uint8_t type = data[offset++];
        
        /* 读取 Length (Big Endian) */
        uint16_t len = read_u16_be(&data[offset]);
        offset += 2;
        
        /* 检查 Value 数据是否完整 */
        if (data_len - offset < len) {
            return SL_STATUS_INVALID_PARAMETER;
        }
        
        /* 填充 TLV 结构 */
        out_tlvs[count].type = type;
        out_tlvs[count].len = len;
        out_tlvs[count].value = (len > 0) ? &data[offset] : NULL;
        count++;
        
        /* 跳过 Value */
        offset += len;
    }
    
    *out_count = count;
    return SL_STATUS_OK;
}

/**
 * @brief 检查是否存在指定 Type 的 TLV
 */
bool phone_tlv_has(const phone_tlv_t *tlvs, uint8_t count, uint8_t type)
{
    return phone_tlv_get(tlvs, count, type) != NULL;
}

/**
 * @brief 获取指定 Type 的 TLV
 */
const phone_tlv_t *phone_tlv_get(const phone_tlv_t *tlvs, uint8_t count, uint8_t type)
{
    if (tlvs == NULL) return NULL;
    
    for (uint8_t i = 0; i < count; i++) {
        if (tlvs[i].type == type) {
            return &tlvs[i];
        }
    }
    return NULL;
}

/**
 * @brief 从 TLV 读取 U8
 */
bool phone_tlv_read_u8(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint8_t *val)
{
    const phone_tlv_t *tlv = phone_tlv_get(tlvs, count, type);
    if (tlv == NULL || tlv->len < 1) return false;
    if (val) *val = tlv->value[0];
    return true;
}

/**
 * @brief 从 TLV 读取 U16 BE
 */
bool phone_tlv_read_u16(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint16_t *val)
{
    const phone_tlv_t *tlv = phone_tlv_get(tlvs, count, type);
    if (tlv == NULL || tlv->len < 2) return false;
    if (val) *val = read_u16_be(tlv->value);
    return true;
}

/**
 * @brief 从 TLV 读取 U32 BE
 */
bool phone_tlv_read_u32(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint32_t *val)
{
    const phone_tlv_t *tlv = phone_tlv_get(tlvs, count, type);
    if (tlv == NULL || tlv->len < 4) return false;
    if (val) *val = read_u32_be(tlv->value);
    return true;
}

/**
 * @brief 从 TLV 读取 U64 BE
 */
bool phone_tlv_read_u64(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint64_t *val)
{
    const phone_tlv_t *tlv = phone_tlv_get(tlvs, count, type);
    if (tlv == NULL || tlv->len < 8) return false;
    if (val) *val = read_u64_be(tlv->value);
    return true;
}
