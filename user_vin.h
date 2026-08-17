/***************************************************************************//**
 * @file user_vin.h
 * @brief V1.2 VIN 车辆关联状态机
 *
 * 管理 BG24 本地绑定 VIN 与 VIU 当前 VIN 的一致性校验。
 *
 * 四态模型:
 *   VIN_UNLEARNED  — 出厂状态, 未学习 VIN
 *   VIN_MATCH      — 本地 VIN == VIU 当前 VIN
 *   VIN_MISMATCH   — 本地 VIN != VIU 当前 VIN
 *   VIN_UNKNOWN    — VIU 通信异常 / VIN 无效 / 无法确认
 *
 * 规则:
 *   - VIN 仅在首次 APP 绑定时通过 phone_storage_atomic_register_v12() 学习
 *   - 正常上电绝不自学习 VIN
 *   - 状态通过 vehicle_state_get_vin() 获取 VIU 当前 VIN 后比对更新
 ******************************************************************************/
#ifndef USER_VIN_H
#define USER_VIN_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== VIN 关联状态 ==================== */
#define VIN_STATE_UNLEARNED  0x00U  /* 未学习 (出厂状态) */
#define VIN_STATE_MATCH      0x01U  /* 匹配 */
#define VIN_STATE_MISMATCH   0x02U  /* 不匹配 */
#define VIN_STATE_UNKNOWN    0x03U  /* 状态未知 */

/* ==================== 抽象关联结果 (给 APP) ==================== */
#define VIN_ASSOC_NORMAL      0x00U  /* 车辆关联正常 */
#define VIN_ASSOC_ABNORMAL    0x01U  /* 车辆关联异常 */
#define VIN_ASSOC_UNCONFIRMED 0x02U  /* 车辆关联状态无法确认 */

/* ==================== 配置 ==================== */
#define VIN_LENGTH              17U   /* 标准 VIN 长度 */
#define VIN_REFRESH_INTERVAL_MS  1000U /* VIN 状态定期比对间隔 */

/* ==================== 公开 API ==================== */

/**
 * @brief 初始化 VIN 状态机 (上电时调用, 在 phone_storage_init 之后)
 */
void user_vin_init(void);

/**
 * @brief 定期维护: 比对 VIU 当前 VIN 与本地 VIN, 更新状态
 *        需在主循环中周期性调用
 */
void user_vin_process(void);

/**
 * @brief 学习 VIN (仅首次 APP 绑定成功时调用)
 * @param vin 17 字节 ASCII VIN
 * @return SL_STATUS_OK 成功
 */
sl_status_t user_vin_learn(const uint8_t vin[17]);

/**
 * @brief 获取当前 VIN 关联状态
 * @return VIN_STATE_UNLEARNED / MATCH / MISMATCH / UNKNOWN
 */
uint8_t user_vin_get_state(void);

/**
 * @brief 便捷查询: 当前 VIN 是否匹配
 */
bool user_vin_is_match(void);

/**
 * @brief 是否已学习 VIN (EEPROM 中有有效 VIN)
 */
bool user_vin_is_learned(void);

/**
 * @brief 获取抽象关联状态 (给 APP, 不暴露 VIN)
 * @return VIN_ASSOC_NORMAL / ABNORMAL / UNCONFIRMED
 */
uint8_t user_vin_get_abstract_status(void);

/**
 * @brief 获取本地存储的 VIN
 * @param vin [out] 17 字节缓冲区
 * @return SL_STATUS_OK 成功
 */
sl_status_t user_vin_get_local_vin(uint8_t vin[17]);

/**
 * @brief 清除本地 VIN (工厂复位调用)
 */
sl_status_t user_vin_clear(void);

/**
 * @brief 获取 VIN 状态名称 (调试用)
 */
const char *user_vin_state_name(uint8_t state);

#ifdef __cplusplus
}
#endif

#endif /* USER_VIN_H */
