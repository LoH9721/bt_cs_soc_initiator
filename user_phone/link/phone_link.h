/***************************************************************************//**
 * @file phone_link.h
 * @brief 手机链路面：广播、连接管理（不含协议/GATT 载荷处理）。
 ******************************************************************************/
#ifndef PHONE_LINK_H
#define PHONE_LINK_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_bt_api.h"

#ifdef __cplusplus
extern "C" {
#endif

void phone_link_init(void);
void phone_link_process_action(void);
void phone_link_on_bt_event(sl_bt_msg_t *evt);
void phone_link_switch_to_secondary(void);
void phone_link_switch_to_primary(void);
uint8_t phone_link_conn_handle(void);

/** 当前连接是否关联了栈内 Bond；不表示该 Bond 已被 APP 授权。 */
bool phone_link_current_is_bonded(void);

/** 当前连接是否处于 BLE 加密状态。 */
bool phone_link_current_is_encrypted(void);

/** 当前连接的 Bond handle；未关联 Bond 时返回 SL_BT_INVALID_BONDING_HANDLE。 */
uint8_t phone_link_current_bonding_handle_get(void);

/** 当前连接的 BLE Security Mode；未连接时返回 Mode 1 Level 1。 */
uint8_t phone_link_current_security_mode_get(void);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_LINK_H */
