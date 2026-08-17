/***************************************************************************//**
 * @file user_app_peps.c
 * @brief 组合 PEPS: Key + Phone 解闭锁命令仲裁
 *
 * 仅提供数据接口供 user_app_fun.c 调用, 不直接操作 CAN。
 * CAN 输出统一在 user_app_fun.c 的 bridge_ble_to_can_tx() 中完成。
 *
 * 当前阶段: Key 侧 auto_cmd 直通。
 * 后续: 与 Phone 侧 PEPS 命令做优先级/互斥/安全仲裁。
 ******************************************************************************/
#include "user_app_peps.h"
#include "user_app_key_peps.h"
#include "user_log_console.h"


static uint8_t g_pending_lock_cmd;   /* one-shot 锁命令 */


void user_app_peps_init(void)
{
    g_pending_lock_cmd = 0;
    USER_LOG_INFO("[PEPS] init done");
}

void user_app_peps_process(void)
{
    /* 从 Key PEPS 获取自动命令 */
    uint8_t key_cmd = user_app_key_peps_get_auto_cmd();

    /* TODO: 获取 Phone PEPS 命令, 与 Key 命令做仲裁 */

    if (key_cmd != 0 && g_pending_lock_cmd == 0) {
        g_pending_lock_cmd = key_cmd;

        USER_LOG_INFO("[PEPS] lock_cmd=%s (from key)" USER_LOG_NL,
                      key_cmd == 1 ? "UNLOCK" : "LOCK");
    }
}

uint8_t user_app_peps_get_lock_cmd(void)
{
    uint8_t cmd = g_pending_lock_cmd;
    g_pending_lock_cmd = 0;
    return cmd;
}

uint8_t user_app_peps_get_zone(void)
{
    return user_app_key_peps_get_zone();
}

float user_app_peps_get_distance(void)
{
    return user_app_key_peps_get_distance();
}

bool user_app_peps_is_distance_valid(void)
{
    return user_app_key_peps_is_valid();
}
