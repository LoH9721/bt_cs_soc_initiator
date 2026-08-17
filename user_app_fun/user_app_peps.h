#ifndef USER_APP_PEPS_H
#define USER_APP_PEPS_H

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief 组合 PEPS 模块: Key + Phone 解闭锁命令仲裁
 *
 * 仅提供数据接口, 不直接操作 CAN。CAN 输出统一在 user_app_fun.c 中完成。
 */
void user_app_peps_init(void);
void user_app_peps_process(void);

/** 获取仲裁后的锁命令: 0=无, 1=解锁, 2=闭锁 (消费后清零) */
uint8_t user_app_peps_get_lock_cmd(void);

/** 获取钥匙位置区 (转发 user_app_key_peps) */
uint8_t user_app_peps_get_zone(void);

/** 获取融合距离 (m, 转发 user_app_key_peps) */
float user_app_peps_get_distance(void);

/** 融合距离是否有效 */
bool user_app_peps_is_distance_valid(void);

#endif /* USER_APP_PEPS_H */
