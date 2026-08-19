#ifndef PHONE_SM_H
#define PHONE_SM_H

#include <stdint.h>
#include <stdbool.h>
#include "sl_status.h"
#include "user_phone/phone_cfg.h"
#include "user_phone/data/phone_frame.h"
#include "user_phone/data/phone_tlv.h"
#include "user_phone/data/phone_session.h"

#ifdef __cplusplus
extern "C" {
#endif

/* 以下常量从 phone_cfg.h 引用, phone_tlv 类型从 phone_tlv.h 引用 */

/* phone_session_t 定义见 phone_session.h */

/* ==================== 回调函数类型 ==================== */

/**
 * @brief GATT 发送回调
 */
typedef sl_status_t (*phone_sm_send_fn_t)(const uint8_t *data, uint16_t len);

/**
 * @brief 断开连接回调
 */
typedef void (*phone_sm_disconnect_fn_t)(void);

/**
 * @brief 状态变化回调
 */
typedef void (*phone_sm_state_change_fn_t)(uint8_t device_state, uint8_t vehicle_lock_state);

/**
 * @brief 控制命令执行回调 (由上层业务实现具体动作)
 */
typedef void (*phone_sm_ctrl_exec_fn_t)(uint8_t control_cmd);

/* ==================== 公开API ==================== */

/**
 * @brief 处理定时任务 (需在主循环中周期性调用)
 */
void phone_sm_process_action(void);

/**
 * @brief 连接打开回调
 */
void phone_sm_on_connection_opened(uint8_t conn_handle, uint8_t bonding);

/**
 * @brief 连接关闭回调
 */
void phone_sm_on_connection_closed(uint8_t conn_handle, uint8_t bonding,
                                   uint8_t security_mode);

/**
 * @brief MTU交换完成回调
 */
void phone_sm_on_mtu_exchanged(uint16_t mtu);

/**
 * @brief Notify使能回调
 */
void phone_sm_on_notify_enabled(void);

/**
 * @brief 接收数据处理 (从GATT Notify收到数据时调用)
 * @param data 数据指针
 * @param len 数据长度
 */
void phone_sm_on_receive(const uint8_t *data, uint16_t len);

/**
 * @brief 查询连接状态
 */
bool phone_sm_is_connected(void);

/**
 * @brief 查询认证状态
 */
bool phone_sm_is_authenticated(void);

/**
 * @brief 查询 Tx Notify 是否已使能 (APP 已订阅)
 */
bool phone_sm_is_notify_enabled(void);

/**
 * @brief 查询无感钥匙是否已启用 (passive_enabled)
 */
bool phone_sm_is_passive_enabled(void);

/** 是否处于安全失败静默期 (SILENT) */
bool phone_sm_is_silent(void);

/** 指定运行期 Bond handle 是否对应当前 APP 授权手机。 */
bool phone_sm_is_authorized_bonding(uint8_t bonding);

/** 读取并清除一次授权 Passive L4 链路断开事件。 */
bool phone_sm_consume_authorized_passive_disconnect(void);

/**
 * @brief 消耗一次自动解锁额度 (剩余>0 则 -1 并持久化, 返回 true; 否则 false)
 */
bool phone_sm_consume_passive_quota(void);

/**
 * @brief 查询当前无感剩余额度 (只读, 不消耗)
 */
uint32_t phone_sm_get_passive_quota(void);

/**
 * @brief 查询当前无感灵敏度档位 (1=近 2=标准 3=远)
 */
uint8_t phone_sm_get_passive_sensitivity(void);

/**
 * @brief 获取待处理控制命令 (消费型，读取后清零)
 */
uint8_t phone_sm_get_pending_control_cmd(void);

/**
 * @brief 获取当前锁状态
 */
uint8_t phone_sm_get_vehicle_lock_state(void);

/**
 * @brief 获取当前设备状态
 */
uint8_t phone_sm_get_device_state(void);

/**
 * @brief 通知上层锁状态变化 (由车端触发)
 * @param lock_state 新的锁状态
 */
void phone_sm_notify_vehicle_lock_state(uint8_t lock_state);

/**
 * @brief 设置 PEPS/车辆授权条件 (供串口命令/CAN 侧调用)
 * @param met true=条件满足(默认), false=条件不满足
 */
void phone_sm_set_auth_condition(bool met);

/** V1.2 新增: 完整车辆状态快照变化通知 (4 字段) */
void phone_sm_notify_vehicle_state(uint8_t lock, uint8_t ignition,
                                   uint16_t range, uint8_t doors);

/* V1.2 SM 事件回调 (供 phone_comm.c 调用) */
void phone_sm_on_sm_confirm_bonding(uint8_t connection,
                                    uint8_t bonding_handle);
void phone_sm_on_sm_bonded(uint8_t connection, uint8_t bonding,
                           uint8_t security_mode);
void phone_sm_on_sm_bonding_failed(uint8_t connection, uint16_t reason);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_SM_H */
/***************************************************************************//**
 * @file phone_sm.h
 * @brief V1.1 协议状态机：接收处理流水线 + 13 条命令的完整实现。
 *
 * 处理流程 (协议第 15 / 22.8 节):
 *   1. 帧头/长度校验
 *   2. CRC16 校验
 *   3. 幂等缓存查询
 *   4. SecurityCounter 检查
 *   5. AES-CCM Tag 验证 + 解密
 *   6. TLV 解析与校验
 *   7. 状态/会话/challenge/签名/appCounter 验证
 *   8. 执行业务、原子更新、缓存响应、发送
 ******************************************************************************/
#ifndef PHONE_SM_H
#define PHONE_SM_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// 发送回调
// =============================================================================

/** 发送回调: SM 通过此函数发送 Notify 数据到手机 */
typedef sl_status_t (*phone_sm_send_fn_t)(const uint8_t *data, uint16_t len);

/** 断开连接回调: SM 请求断开连接 */
typedef void (*phone_sm_disconnect_fn_t)(void);

// =============================================================================
// 生命周期
// =============================================================================

/**
 * @brief 初始化协议状态机
 * @param send_fn  发送回调 (Notify)
 * @param disc_fn  断开回调
 */
void phone_sm_init(phone_sm_send_fn_t send_fn, phone_sm_disconnect_fn_t disc_fn);

/** 周期调用: 处理超时 */
void phone_sm_process_action(void);

// =============================================================================
// BLE 事件
// =============================================================================

/** 协议栈启动后校验 Passive 配置与授权 Bond，再决定是否恢复 HID。 */
void phone_sm_on_system_boot(void);

void phone_sm_on_connection_opened(uint8_t conn_handle, uint8_t bonding);
void phone_sm_on_connection_closed(uint8_t conn_handle, uint8_t bonding,
                                   uint8_t security_mode);
void phone_sm_on_mtu_exchanged(uint16_t mtu);

/** Notify (CCCD) 已使能 */
void phone_sm_on_notify_enabled(void);

// =============================================================================
// GATT 数据接收
// =============================================================================

/**
 * @brief 处理手机写入的数据 (明文帧或加密帧)
 * @note 这是协议的主入口, 内部完成全部校验和分发
 */
void phone_sm_on_receive(const uint8_t *data, uint16_t len);

// =============================================================================
// 查询接口 (供 user_app_fun / CAN 桥接使用)
// =============================================================================

/** 是否已连接手机 */
bool phone_sm_is_connected(void);

/** 是否已完成 AUTH 认证 (sessionKey 有效) */
bool phone_sm_is_authenticated(void);

/** 获取并清除缓存的远程控制命令 (0x01=解锁 0x02=闭锁 0x03=寻车, 0x00=无) */
uint8_t phone_sm_get_pending_control_cmd(void);

/** 获取当前 vehicleLockState */
uint8_t phone_sm_get_vehicle_lock_state(void);

/** 获取当前设备状态 (bindState/deviceState) */
uint8_t phone_sm_get_device_state(void);

// =============================================================================
// 车辆状态推送 (供 CAN 桥接调用, 触发 STATE_CHANGED_EVENT)
// =============================================================================

/**
 * @brief 通知车辆锁状态变化 (由 CAN 侧调用)
 * @param lock_state 新的锁状态 (PHONE_LOCK_STATE_*)
 * @note 仅在 AUTH 成功后且状态确实变化时才发送 EVENT
 */
void phone_sm_notify_vehicle_lock_state(uint8_t lock_state);

/** V1.2 新增: 完整车辆状态快照变化通知 (4 字段: lock/ignition/range/doors) */
void phone_sm_notify_vehicle_state(uint8_t lock, uint8_t ignition,
                                   uint16_t range, uint8_t doors);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_SM_H */
