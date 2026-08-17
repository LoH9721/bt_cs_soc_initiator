/***************************************************************************//**
 * @file user_app_phone_peps.h
 * @brief 手机 PEPS 模块: RSSI 距离 → 迟滞区域判断 → 去抖 → 自动解闭锁
 *
 * 数据流:
 *   phone_rang (卡尔曼滤波距离) → 迟滞区域判定 → 去抖计数(5次)
 *   → zone 跳变检测 → one-shot 自动命令 (+2s冷却)
 *
 * 6 个迟滞阈值可通过串口命令标定, 存储在 EEPROM 0x5400 范围。
 ******************************************************************************/
#ifndef USER_APP_PHONE_PEPS_H
#define USER_APP_PHONE_PEPS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 初始化: 从 EEPROM 加载阈值, 初始化状态 */
void user_app_phone_peps_init(void);

/** 主循环: 获取距离 → 迟滞区域判断 → 去抖 → 自动解闭锁命令 */
void user_app_phone_peps_process(void);

/** 获取当前区域 (返回 APP_PROTO_ZONE_*: 0=未知 1=车内 2=解锁区 3=闭锁区 4=无效区) */
uint8_t user_app_phone_peps_get_zone(void);

/** 获取并清除一次性自动解闭锁命令 (0=无 1=解锁 2=闭锁) */
uint8_t user_app_phone_peps_get_auto_cmd(void);

/** 获取当前融合距离 (m) */
float user_app_phone_peps_get_distance(void);

/** 融合距离是否有效 */
bool user_app_phone_peps_is_distance_valid(void);

/** 断连通知: 重置所有状态 */
void user_app_phone_peps_on_disconnected(void);

/* ========== 阈值管理 (串口标定用) ========== */

/** 阈值索引 */
#define PHONE_PEPS_TH_IDX_IN_ENTER        0
#define PHONE_PEPS_TH_IDX_IN_EXIT         1
#define PHONE_PEPS_TH_IDX_UNLOCK_ENTER    2
#define PHONE_PEPS_TH_IDX_UNLOCK_EXIT     3
#define PHONE_PEPS_TH_IDX_INVALID_ENTER   4
#define PHONE_PEPS_TH_IDX_INVALID_EXIT    5

/** 设置单个阈值 (立即生效, 异步写入 EEPROM) */
void user_app_phone_peps_threshold_set(uint8_t index, uint16_t value_cm);

/** 读取单个阈值 (cm) */
uint16_t user_app_phone_peps_threshold_get(uint8_t index);

/** 检查某个阈值是来自 EEPROM (true) 还是使用默认值 (false) */
bool user_app_phone_peps_threshold_is_from_eeprom(uint8_t index);

/** 重置所有阈值到编译期默认值 (清除 EEPROM 条目) */
void user_app_phone_peps_threshold_reset(void);

/** 从 EEPROM 重新加载所有阈值 */
void user_app_phone_peps_threshold_reload(void);

/* ========== 三档灵敏度走近解锁距离 (标定用) ========== */

/** 三档灵敏度距离索引 (对应协议档位 - 1) */
#define PHONE_PEPS_SENS_IDX_NEAR      0U   /* 档位 1 (近)   */
#define PHONE_PEPS_SENS_IDX_STANDARD  1U   /* 档位 2 (标准) */
#define PHONE_PEPS_SENS_IDX_FAR       2U   /* 档位 3 (远)   */

/** 三档默认走近解锁距离 (cm) */
#define PHONE_PEPS_DEFAULT_SENS_NEAR_CM   300U
#define PHONE_PEPS_DEFAULT_SENS_STD_CM    700U
#define PHONE_PEPS_DEFAULT_SENS_FAR_CM    850U

/** 设置某档 (1=近 2=标准 3=远) 的走近解锁距离 (立即生效, 异步写入 EEPROM) */
void user_app_phone_peps_sens_dist_set(uint8_t level, uint16_t value_cm);

/** 读取某档 (1-3) 的走近解锁距离 (cm) */
uint16_t user_app_phone_peps_sens_dist_get(uint8_t level);

/** 重置三档距离到编译期默认值 (清除 EEPROM 条目) */
void user_app_phone_peps_sens_dist_reset(void);

/** 三档距离是否来自 EEPROM (true) 还是默认值 (false) */
bool user_app_phone_peps_sens_dist_is_from_eeprom(void);

/** 当前档位生效的走近解锁距离 (cm, 已按 unlock_exit 迟滞钳制) */
uint16_t user_app_phone_peps_sens_dist_effective(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_APP_PHONE_PEPS_H */
