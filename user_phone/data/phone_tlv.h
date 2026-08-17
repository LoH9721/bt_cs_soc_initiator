#ifndef PHONE_TLV_H
#define PHONE_TLV_H

#include <stdint.h>
#include <stdbool.h>
#include "sl_status.h"
#include "user_phone/phone_cfg.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== TLV 类型定义 (协议第7章) ==================== */
/***************************************************************************//**
 * @file phone_tlv.h
 * @brief V1.1 TLV 编解码：Type(U8) | Length(U16 BE) | Value(N)
 *
 * 协议第 7 节 TLV 通用规则:
 *   - 未知可选 TLV 可忽略
 *   - 必选 TLV 缺失返回 PARAM_INVALID
 *   - 同一 Type 重复出现应拒绝 (除非命令明确允许)
 *   - 字符串使用 UTF-8, 不带结尾 00
 ******************************************************************************/

/* TLV 类型和长度常量从 phone_cfg.h 引用 */

/* 用户自定义扩展区 */
#define PHONE_TLV_USER_CUSTOM_START    0x80

#define PHONE_TLV_MAX_COUNT            32U   /* 单帧最多 TLV 数量 */

// =============================================================================
// TLV 条目
// =============================================================================

typedef struct {
  uint8_t        type;    /* TLV Type (U8) */
  uint16_t       len;     /* Value 长度 (U16 BE) */
  const uint8_t *value;   /* 指向 Value 数据 (在原始缓冲区中, 不拷贝) */
} phone_tlv_t;

// =============================================================================
// TLV 编码
// =============================================================================

/**
 * @brief 将 TLV 数组编码为 Payload 字节流
 * @param tlvs       TLV 数组
 * @param tlv_count  TLV 数量
 * @param out_buf    输出缓冲区
 * @param out_len    输出长度
 * @param buf_cap    缓冲区容量
 * @return SL_STATUS_OK 成功, SL_STATUS_INVALID_PARAMETER 缓冲区不足
 */
sl_status_t phone_tlv_encode(const phone_tlv_t *tlvs, uint8_t tlv_count,
                             uint8_t *out_buf, uint16_t *out_len,
                             uint16_t buf_cap);

// =============================================================================
// TLV 解码
// =============================================================================

/**
 * @brief 解析 Payload 为 TLV 数组
 * @param data       Payload 数据
 * @param data_len   Payload 长度
 * @param out_tlvs   输出 TLV 数组 (指向原始缓冲区)
 * @param out_count  输出 TLV 数量
 * @param max_count  最大 TLV 数量
 * @return SL_STATUS_OK 成功
 *         SL_STATUS_INVALID_PARAMETER 格式错误 (长度不足/非法)
 */
sl_status_t phone_tlv_parse(const uint8_t *data, uint16_t data_len,
                            phone_tlv_t *out_tlvs, uint8_t *out_count,
                            uint8_t max_count);

// =============================================================================
// TLV 查询
// =============================================================================

/** 检查是否存在指定 Type 的 TLV */
bool phone_tlv_has(const phone_tlv_t *tlvs, uint8_t count, uint8_t type);

/** 获取指定 Type 的 TLV, 不存在返回 NULL */
const phone_tlv_t *phone_tlv_get(const phone_tlv_t *tlvs, uint8_t count, uint8_t type);

/** 检查 TLV 数组是否有重复 Type */
bool phone_tlv_has_duplicates(const phone_tlv_t *tlvs, uint8_t count);

// =============================================================================
// TLV 校验 (用于命令 Schema 校验)
// =============================================================================

/**
 * @brief 校验必选 TLV 是否全部存在, 并检查是否有非法重复
 * @param tlvs          解析后的 TLV 数组
 * @param count         TLV 数量
 * @param required_types 必选 Type 数组
 * @param req_count     必选 Type 数量
 * @return SL_STATUS_OK 通过, SL_STATUS_INVALID_PARAMETER 缺失必选项
 */
sl_status_t phone_tlv_validate_required(const phone_tlv_t *tlvs, uint8_t count,
                                        const uint8_t *required_types, uint8_t req_count);

/**
 * @brief 校验可选 TLV 的长度是否合法
 * @param type   TLV Type
 * @param len    Value 长度
 * @return true 合法
 */
bool phone_tlv_validate_length(uint8_t type, uint16_t len);

// =============================================================================
// TLV 值读取工具 (从 TLV 中提取单值)
// =============================================================================

/** 从 TLV 读取 U8, TLV 不存在返回 false */
bool phone_tlv_read_u8(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint8_t *val);

/** 从 TLV 读取 U16 BE */
bool phone_tlv_read_u16(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint16_t *val);

/** 从 TLV 读取 U32 BE */
bool phone_tlv_read_u32(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint32_t *val);

/** 从 TLV 读取 U64 BE */
bool phone_tlv_read_u64(const phone_tlv_t *tlvs, uint8_t count, uint8_t type, uint64_t *val);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_TLV_H */