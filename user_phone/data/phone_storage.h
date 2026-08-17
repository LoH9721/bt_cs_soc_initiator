/***************************************************************************//**
 * @file phone_storage.h
 * @brief V1.1 手机协议持久化存储 — 基于 user_eeprom 统一存储层。
 *
 * 所有 NVM key 地址统一定义在 user_eeprom_items.def 中,
 * 本模块仅提供类型化的 getter/setter 封装, 不自行定义地址。
 *
 * 原子换绑: 备用区写入 → 回读校验 → 覆盖主区 → 清除备用区。
 ******************************************************************************/
#ifndef PHONE_STORAGE_H
#define PHONE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// NVM Key 地址已迁移到 user_eeprom_items.def (统一管理 + 自动 CRC16)
// 本模块不再定义 PHONE_NVM_KEY_* 宏, 改为通过 user_eeprom_read/write 操作
// =============================================================================

// =============================================================================
// 初始化与工厂复位
// =============================================================================

/** 初始化 NVM 存储, 清除残留的换绑备用数据 */
sl_status_t phone_storage_init(void);

/** 上电后一次性打印所有存储信息 (产线数据 + 绑定记录) */
void phone_storage_dump_all(void);

/**
 * @brief 工厂复位: 清除所有绑定记录, 保留 deviceId/qid/cv/bindSecret/厂家公钥
 */
sl_status_t phone_storage_factory_reset(void);

// =============================================================================
// 产线数据 (qid/cv/bindSecret)
// =============================================================================

/** 获取 deviceId (上电时从芯片 64-bit Unique ID 派生, RAM 缓存, 总有效) */
sl_status_t phone_storage_get_device_id(char *buf, uint8_t max_len);

sl_status_t phone_storage_get_qid(char *buf, uint8_t max_len);
sl_status_t phone_storage_set_qid(const char *qid);

uint32_t phone_storage_get_cv(void);
sl_status_t phone_storage_set_cv(uint32_t cv);

sl_status_t phone_storage_get_bind_secret(uint8_t secret[16]);
sl_status_t phone_storage_set_bind_secret(const uint8_t secret[16]);

// =============================================================================
// 厂家公钥
// =============================================================================

sl_status_t phone_storage_get_factory_pubkey(uint8_t pubkey[65]);
sl_status_t phone_storage_set_factory_pubkey(const uint8_t pubkey[65]);

// =============================================================================
// 绑定记录 (APP 公钥 / appKeyId / bindVersion / bindState)
// =============================================================================

sl_status_t phone_storage_get_app_public_key(uint8_t key[65]);
sl_status_t phone_storage_get_app_key_id(uint8_t id[16]);

uint32_t phone_storage_get_bind_version(void);
uint8_t  phone_storage_get_bind_state(void);

/** 直接写入 bindState */
sl_status_t phone_storage_set_bind_state(uint8_t state);

// =============================================================================
// 首次绑定: 原子写入 (备用区 → 主区)
// =============================================================================

/**
 * @brief 原子写入首次绑定记录 (备用区 → 回读校验 → 主区 → 清除备用)
 *
 * V1.2: 首次绑定时同时原子写入 VIN。
 *
 * @param public_key   APP 公钥 (65 Byte)
 * @param key_id       BG24 生成的 appKeyId (16 Byte)
 * @param bind_version 绑定版本 (初次=1)
 * @param vin          车辆 VIN (17 字节 ASCII, NULL=不写入 VIN/换绑场景)
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_storage_atomic_register(const uint8_t public_key[65],
                                          const uint8_t key_id[16],
                                          uint32_t bind_version,
                                          const uint8_t *vin);

// =============================================================================
// 换绑: 原子替换 (备用区写入 → 回读 → 主区覆盖 → 清除备用)
// =============================================================================

/**
 * @brief 原子换绑 (备用区写入 → 回读校验 → 原子切换 → 清除备用)
 * @param new_public_key  新 APP 公钥 (65 Byte)
 * @param new_key_id      新 appKeyId (16 Byte)
 * @param new_bind_version 新 bindVersion (旧+1)
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_storage_atomic_rebind(const uint8_t new_public_key[65],
                                        const uint8_t new_key_id[16],
                                        uint32_t new_bind_version);

// =============================================================================
// appCounter 持久化
// =============================================================================

/**
 * @brief 获取持久化的 appCounter (已接受的最大值)
 */
uint64_t phone_storage_get_app_counter(void);

/**
 * @brief 原子提交 appCounter (received > stored 时调用)
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_storage_commit_app_counter(uint64_t counter);

// =============================================================================
// V1.2 PASSIVE 状态持久化
// =============================================================================

sl_status_t phone_storage_get_passive_enabled(bool *enabled);
sl_status_t phone_storage_set_passive_enabled(bool enabled);
sl_status_t phone_storage_get_passive_sensitivity(uint8_t *sens);
sl_status_t phone_storage_set_passive_sensitivity(uint8_t sens);
sl_status_t phone_storage_get_passive_quota(uint32_t *quota);
sl_status_t phone_storage_set_passive_quota(uint32_t quota);

// =============================================================================
// 存在性检查
// =============================================================================

/** 是否已有有效的绑定记录 (APP 公钥已注册) */
bool phone_storage_is_bound(void);

/** 产线数据 (bindSecret) 是否已写入 */
bool phone_storage_is_provisioned(void);

// =============================================================================
// VIN 车辆识别码 (17 字节, 工厂复位清除)
// =============================================================================

/** 获取存储的 VIN (17 字节, 不含 '\0') */
sl_status_t phone_storage_get_vin(uint8_t vin[17]);

/** VIN 是否已存储 (CRC 校验有效) */
bool phone_storage_has_vin(void);

/** 清除已存储的 VIN */
sl_status_t phone_storage_clear_vin(void);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_STORAGE_H */
