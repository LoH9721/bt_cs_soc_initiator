/***************************************************************************//**
 * @file user_vehicle_state.h
 * @brief V1.2 车辆状态抽象接口 — CAN 未定期间的开发和测试层
 *
 * 设计: 双数据源模式
 *   - CAN 来源 (RTE 信号) — 实车联调时使用
 *   - 串口注入 (手动 override) — 开发和测试时使用
 *
 * 上层 phone_sm / user_vin 只调用本模块的 getter, 不直接访问 CAN/RTE。
 ******************************************************************************/
#ifndef USER_VEHICLE_STATE_H
#define USER_VEHICLE_STATE_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ==================== 点火档位 ==================== */
#define IGNITION_OFF   0x00U
#define IGNITION_ACC   0x01U
#define IGNITION_ON    0x02U
#define IGNITION_START 0x03U

/* ==================== 门状态 bitmask ==================== */
#define DOOR_FL_MASK   0x01U  /* 左前门 */
#define DOOR_FR_MASK   0x02U  /* 右前门 */
#define DOOR_RL_MASK   0x04U  /* 左后门 */
#define DOOR_RR_MASK   0x08U  /* 右后门 */

/* ==================== 锁状态 ==================== */
#define FINAL_LOCK_UNKNOWN  0x00U
#define FINAL_LOCK_LOCKED   0x01U
#define FINAL_LOCK_UNLOCKED 0x02U

/* ==================== VIN 超时 ==================== */
#define VIN_TIMEOUT_MS  5000U  /* 超过此时间未收到 VIU VIN → 标记无效 (容忍网络波动) */

/* ==================== 公开 API — VIN ==================== */

/** VIN 当前是否有效 (最近 VIN_TIMEOUT_MS 内收到) */
bool vehicle_state_is_vin_available(void);

/** 获取缓存的 VIU VIN (17 字节 ASCII) */
sl_status_t vehicle_state_get_vin(uint8_t vin[17]);

/** [串口] 手动注入 VIN */
void vehicle_state_set_vin_override(const uint8_t vin[17]);

/** [串口] 清除 VIN 手动注入, 恢复 CAN 来源 */
void vehicle_state_clear_vin_override(void);

/* ==================== 公开 API — 车辆状态 ==================== */

/** PEPS 钥匙是否在车内 */
bool vehicle_state_is_peps_key_in_car(void);

/** 获取点火档位 (IGNITION_OFF/ACC/ON/START) */
uint8_t vehicle_state_get_ignition_gear(void);

/** 获取剩余续航 (km) */
uint16_t vehicle_state_get_remaining_range_km(void);

/** 获取门状态 bitmask (DOOR_FL_MASK | DOOR_FR_MASK | ...) */
uint8_t vehicle_state_get_door_status(void);

/** 获取 VIU 最终锁状态 */
uint8_t vehicle_state_get_final_lock_state(void);

/** [串口] 手动注入车辆状态 */
void vehicle_state_set_override(uint8_t ignition, uint16_t range,
                                uint8_t doors, uint8_t lock_state, bool peps_key);

/** [串口] 清除手动注入, 恢复 CAN 来源 */
void vehicle_state_clear_override(void);

/** 当前是否使用串口手动注入数据 */
bool vehicle_state_is_override_active(void);

/* ==================== 公开 API — 生命周期 ==================== */

/** 初始化 (上电时调用) */
void vehicle_state_init(void);

/** 周期性维护: 检查 VIN 超时等 */
void vehicle_state_process(void);

/* ==================== 内部接口: CAN → 状态同步 (供 user_app_fun 调用) ==================== */

/** CAN 侧 VIN 数据更新 (由 CAN RX 回调调用) */
void vehicle_state_update_vin_from_can(const uint8_t *vin_data, uint8_t len, bool valid);

/** CAN 侧车辆状态更新 (由 CAN RX 回调调用) */
void vehicle_state_update_from_can(uint8_t ignition, uint16_t range,
                                   uint8_t doors, uint8_t lock_state, bool peps_key);

#ifdef __cplusplus
}
#endif

#endif /* USER_VEHICLE_STATE_H */
