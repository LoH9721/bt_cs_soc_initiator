/***************************************************************************//**
 * @file phone_link.h
 * @brief 手机链路面：广播、连接管理（不含协议/GATT 载荷处理）。
 ******************************************************************************/
#ifndef PHONE_LINK_H
#define PHONE_LINK_H

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

#ifdef __cplusplus
}
#endif

#endif /* PHONE_LINK_H */
