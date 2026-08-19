/***************************************************************************//**
 * @file phone_comm.h
 * @brief 手机通信门面：组合链路 + 协议状态机 + RSSI 测距。
 *
 * V1.1 协议完全实现在 data/ 子目录下的模块中:
 *   phone_frame / phone_tlv / phone_crypto / phone_session / phone_storage / phone_sm / phone_adv
 *
 * 对外接口保持向后兼容, 新增 V1.1 专用接口供 user_app_fun 使用。
 ******************************************************************************/
#ifndef PHONE_COMM_H
#define PHONE_COMM_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_bt_api.h"
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// 生命周期 (保持兼容)
// =============================================================================

void phone_comm_init(void);
void phone_comm_process_action(void);
void phone_comm_on_bt_event(sl_bt_msg_t *evt);

// =============================================================================
// 已有接口 (保持兼容)
// =============================================================================

bool phone_comm_is_connected(void);
sl_status_t phone_comm_send(const uint8_t *data, uint16_t len);
uint8_t phone_comm_connection_handle_get(void);
bool phone_comm_normal_actions_allowed(void);

/** 当前连接是否关联了栈内 Bond；不表示该 Bond 已被 APP 授权。 */
bool phone_comm_current_link_is_bonded(void);

/** 当前连接是否处于 BLE 加密状态。 */
bool phone_comm_current_link_is_encrypted(void);

/** 当前连接的 Bond handle；未关联 Bond 时返回 SL_BT_INVALID_BONDING_HANDLE。 */
uint8_t phone_comm_current_bonding_handle_get(void);

/** 当前连接的 BLE Security Mode；未连接时返回 Mode 1 Level 1。 */
uint8_t phone_comm_current_security_mode_get(void);

/** 当前是否为 Passive ON、授权 Bond 且已达到 L4 的手机链路。 */
bool phone_comm_current_link_is_authorized_passive(void);

/** 读取并清除一次授权 Passive L4 链路断开事件。 */
bool phone_comm_consume_authorized_passive_disconnect(void);

/** 切换到副广播 (0xA5) + 超时恢复 */
void phone_comm_switch_to_secondary(void);
void phone_comm_switch_to_primary(void);

/** 恢复出厂前断开手机连接 */
void phone_comm_disconnect_for_factory_reset(void);

// =============================================================================
// V1.1 新增: 控制命令 & 锁状态 (供 user_app_fun 使用)
// =============================================================================

/** 获取并清除缓存的远程控制命令 (0x01=解锁 0x02=闭锁 0x03=寻车, 0=无) */
uint8_t phone_comm_get_pending_control_cmd(void);

/** 获取当前 vehicleLockState */
uint8_t phone_comm_get_vehicle_lock_state(void);

/** CAN 侧通知车辆锁状态变化, 触发 STATE_CHANGED_EVENT */
void phone_comm_notify_vehicle_lock_state(uint8_t lock_state);

/** V1.2 新增: CAN 侧通知完整车辆状态变化 (lock/ignition/range/doors) */
void phone_comm_notify_vehicle_state(uint8_t lock, uint8_t ignition,
                                     uint16_t range, uint8_t doors);

/** 设置 PEPS/车辆授权条件 (供串口命令/CAN 侧调用) */
void phone_comm_set_auth_condition(bool met);

/** 是否已完成 AUTH 认证 (sessionKey 有效) */
bool phone_comm_is_authenticated(void);

/** Tx Notify 是否已使能 (APP 已订阅 CCCD) */
bool phone_comm_is_notify_enabled(void);

/** 无感钥匙是否已启用 (passive_enabled, NVM 持久化) */
bool phone_comm_is_passive_enabled(void);

/** 是否处于安全失败静默期 (SILENT) */
bool phone_comm_is_silent(void);

/** 消耗一次无感自动解锁额度 (额度耗尽返回 false) */
bool phone_comm_consume_passive_quota(void);

/** 查询当前无感剩余额度 (只读, 不消耗) */
uint32_t phone_comm_get_passive_quota(void);

/** 查询当前无感灵敏度档位 (1=近 2=标准 3=远) */
uint8_t phone_comm_get_passive_sensitivity(void);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_COMM_H */
