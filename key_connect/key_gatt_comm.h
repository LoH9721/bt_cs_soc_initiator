/***************************************************************************//**
 * @file
 * @brief GATT 通讯模块 — 管理反射器自定义服务的发现、CCCD 使能及数据收发
 *
 * 通过 BLE GATT 协议与反射器的自定义服务通信:
 *   - Service:  db86d78f-61f5-49e8-98b4-2a2e8e217903
 *   - RX Char:  95221a3f-c6da-491a-b076-0429238d26cf (Write Without Response → 发送)
 *   - TX Char:  db791eff-dfc5-4714-88b2-3db1b07d60f4 (Notify → 接收)
 *
 * 状态机: IDLE → DISCOVER_SERVICES → DISCOVER_CHARACTERISTICS → ENABLE_CCCD → READY
 * 触发条件: 加密完成 (sm_bonded) 或重连时已加密 (connection_parameters + security_status)
 *
 * 调用拓扑:
 *   app.c init                   → key_gatt_comm_set_ready_callback() (注册 cs_key_rang 回调)
 *   key_gatt_cmd_init()          → key_gatt_comm_set_rx_callback() (注册命令解析回调)
 *   key_gatt_cmd_send()          → key_gatt_comm_send() (发送数据)
 *   app_process_action()         → key_gatt_comm_process_action() (安全网兜底)
 *   sl_bt_on_event()             → key_gatt_comm_on_bt_event() (BLE 事件驱动状态机)
 ******************************************************************************/
#ifndef KEY_GATT_COMM_H
#define KEY_GATT_COMM_H

#include "sl_bt_api.h"

/** GATT 数据接收回调: conn_handle + 数据指针 + 长度。由 key_gatt_cmd 模块注册。 */
typedef void (*key_gatt_comm_rx_callback_t)(uint8_t conn, const uint8_t *data, uint8_t len);

/** GATT 就绪回调: 当 CCCD 使能完毕、状态机进入 READY 时触发。由 cs_key_rang 模块注册。 */
typedef void (*key_gatt_comm_ready_cb_t)(uint8_t connection);

/**
 * 模块初始化 (重置状态机为 IDLE)。当前未被显式调用 (静态变量默认为 IDLE)。
 * 保留用于未来显式复位。
 */
void key_gatt_comm_init(void);

/**
 * BLE 事件分发。sl_bt_on_event() 调用。
 * 内部驱动 GATT 发现状态机:
 *   sl_bt_evt_connection_closed → 断线复位到 IDLE
 *   sl_bt_evt_sm_bonded → 加密完成 → 启动 service discovery
 *   sl_bt_evt_connection_parameters → 重连时检查已加密 → 启动 discovery
 *   sl_bt_evt_gatt_service → 记录 service handle
 *   sl_bt_evt_gatt_characteristic → 匹配并记录 RX/TX characteristic handle
 *   sl_bt_evt_gatt_procedure_completed → 推进状态机 (services→chars→cccd→ready)
 *   sl_bt_evt_gatt_characteristic_value → 收到数据 → 调用 rx_callback
 */
void key_gatt_comm_on_bt_event(sl_bt_msg_t *evt);

/**
 * 主循环轮询。app_process_action() 每 tick 调用。
 * 安全网: 若已在 IDLE 但连接已加密 → 启动 GATT 发现 (兜底 sm_bonded 漏掉的情况)。
 */
void key_gatt_comm_process_action(void);

/**
 * 注册数据接收回调。key_gatt_cmd_init() 启动时调用。
 * 当收到反射器 Notify 数据时调用此回调。
 */
void key_gatt_comm_set_rx_callback(key_gatt_comm_rx_callback_t callback);

/**
 * 注册 GATT 就绪回调。app.c init 阶段调用，传入 cs_key_rang_trigger_capabilities_read。
 * 当 CCCD 使能完毕进入 READY 状态时触发。
 */
void key_gatt_comm_set_ready_callback(key_gatt_comm_ready_cb_t cb);

/**
 * 通过 GATT Write Without Response 向反射器发送数据。
 * key_gatt_cmd 模块调用。
 * 内部: 校验 KGATT_READY → sl_bt_gatt_write_characteristic_value_without_response。
 * @return SL_STATUS_OK 成功；SL_STATUS_NOT_READY (GATT 未就绪)；SL_STATUS_NOT_FOUND (RX 特征未发现)。
 */
sl_status_t key_gatt_comm_send(uint8_t conn, const uint8_t *data, uint8_t len);

/**
 * 查询 GATT 是否就绪 (状态机处于 KGATT_READY)。
 * (当前仅有声明，未实现定义；保留用于未来外部状态查询。)
 */
bool key_gatt_comm_is_ready(void);

/**
 * 获取当前连接句柄。若未连接则返回 SL_BT_INVALID_CONNECTION_HANDLE。
 */
uint8_t key_gatt_comm_get_connection(void);

#endif
