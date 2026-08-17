#ifndef USER_APP_KEY_PEPS_H
#define USER_APP_KEY_PEPS_H

#include <stdbool.h>
#include <stdint.h>

#define USER_APP_KEY_PEPS_MAX_KEYS  4

/** 钥匙位置区枚举 (内部使用) */
typedef enum {
    PEPS_ZONE_UNKNOWN          = 0,  /* 无有效距离 */
    PEPS_ZONE_IN_CAR           = 1,  /* 车内 */
    PEPS_ZONE_OUTSIDE_UNLOCK   = 2,  /* 车外解锁区 */
    PEPS_ZONE_OUTSIDE_LOCK     = 3,  /* 车外闭锁区 */
    PEPS_ZONE_OUTSIDE_INVALID  = 4,  /* 车外无效区 */
} peps_zone_t;

/** 自动解闭锁命令 */
typedef enum {
    PEPS_AUTO_CMD_NONE   = 0,
    PEPS_AUTO_CMD_UNLOCK = 1,
    PEPS_AUTO_CMD_LOCK   = 2,
} peps_auto_cmd_t;


void user_app_key_peps_init(void);
void user_app_key_peps_process(void);

/** 获取主钥匙 zone (返回 peps_zone_t 值) */
uint8_t user_app_key_peps_get_zone(void);

/** 获取并清除一次性自动解闭锁命令 */
uint8_t user_app_key_peps_get_auto_cmd(void);

/** 获取主钥匙融合距离 (m) */
float user_app_key_peps_get_distance(void);

/** 主钥匙是否有有效融合距离 */
bool user_app_key_peps_is_valid(void);

/** 断连通知: 标记所有钥匙无效 */
void user_app_key_peps_on_disconnected(void);

#endif /* USER_APP_KEY_PEPS_H */
