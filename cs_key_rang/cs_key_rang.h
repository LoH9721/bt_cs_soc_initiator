/***************************************************************************//**
 * @file
 * @brief CS Channel Sounding ranging module public API
 *
 * 封装 initiator 的创建、测量、显示以及 BLE / Peer Manager 事件钩子。
 * BLE 连接与 CS 测距解耦: 连接就绪后调用 cs_key_rang_start() 开始测量。
 *
 * 调用拓扑:
 *   app_init()                 → cs_key_rang_init()
 *   app_process_action()       → cs_key_rang_process_action()
 *   sl_bt_on_event()           → cs_key_rang_on_bt_event()
 *   peer_manager_handler       → cs_key_rang_on_peer_manager_event()
 *   GATT 就绪回调              → cs_key_rang_trigger_capabilities_read()
 *   user_console (CLI)         → cs_key_rang_start/stop/get_result/configure...
 ******************************************************************************/
#ifndef CS_KEY_RANG_H
#define CS_KEY_RANG_H

#include <stdbool.h>
#include <stdint.h>

#include "sl_status.h"
#include "sl_bluetooth.h"
#include "ble_peer_manager_common.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Runtime CS / RTL 参数 (仅可在测距未激活时修改)。CLI / console 通过 cs_key_rang_configure() 写入。 */
typedef struct {
  uint8_t cs_main_mode;
  uint8_t cs_sub_mode;
  uint8_t algo_mode;
  uint8_t channel_map_preset;
  uint8_t procedure_scheduling;
  uint16_t max_procedure_count;
} cs_key_rang_config_t;

/** 最新一次测距结果快照 (距离单位为米)。由 cs_key_rang_get_result() 填充。 */
typedef struct {
  float distance_filtered_m;
  float distance_raw_m;
  float likeliness;
  float distance_rssi_m;
  float velocity_mps;
  float bit_error_rate;
  float sub_distance_filtered_m;
  uint32_t measurement_count;
  uint16_t ranging_counter;
} cs_key_rang_result_t;

/**
 * 模块初始化。app_init() 启动时调用一次。
 * 内部: 清零全部 instance 状态 → 加载编译期配置 → 混合模式修正 → 打印启动信息 → 启动 display 定时器。
 */
void cs_key_rang_init(void);

/**
 * 主循环轮询。app_process_action() 每 tick 调用。
 * 内部: ① 处理排队的 start/teardown ② 消费新测量结果/进度 → 限频打印 & 更新 display。
 */
void cs_key_rang_process_action(void);

/**
 * BLE 协议栈事件分发。app.c 中 sl_bt_on_event() 调用。
 * 处理: system_boot (TX power/天线初始化) → connection_parameters (连接间隔就绪判断)
 *       → gatt_mtu_exchanged (记录 MTU) → cs_read_remote_supported_capabilities_complete
 *       (优化间隔 → 创建实例 → 请求连接参数更新)。
 */
void cs_key_rang_on_bt_event(sl_bt_msg_t *evt);

/**
 * Peer Manager 事件分发。app.c 中 peer manager handler 调用。
 * 处理: ON_CONN_OPENED_CENTRAL (连接建立 → CLI 同步 → display 模式设置)
 *       → ON_CONN_CLOSED (断开 → 清理状态 → 删除 instance)
 *       → ERROR (打印日志)。
 */
void cs_key_rang_on_peer_manager_event(ble_peer_manager_evt_type_t *event);

/**
 * 获取编译期默认配置。外部 (CLI/console) 运行时查询。
 * 内部: 将 cs_key_rang_config.h 宏值填入 *cfg。
 */
void cs_key_rang_get_default_config(cs_key_rang_config_t *cfg);

/**
 * 运行时修改 CS 参数。CLI / console 调用，仅当无活跃测距时允许。
 * 内部: 校验 cs_key_rang_is_configurable → 写入 initiator_config/rtl_config → 重新应用 channel map 和混合模式。
 * @return SL_STATUS_BUSY 若任一连接正在测距。
 */
sl_status_t cs_key_rang_configure(const cs_key_rang_config_t *cfg);

/**
 * 查询是否允许修改配置。cs_key_rang_configure() 和 CLI 的守卫。
 * 内部: 遍历全部 instance，任一 ranging_active 则返回 false。
 */
bool cs_key_rang_is_configurable(void);

/**
 * 启动 CS 测距。用户通过 console 命令触发 (如 "rang start")。
 * 内部: 查 instance → 校验 capabilities 已读 + 连接间隔就绪 → 若 teardown 未完成则排队等待
 *       → 调用 cs_key_rang_begin_initiator 创建 initiator。
 * @return SL_STATUS_OK 成功；SL_STATUS_IN_PROGRESS 已排队等待 cs_key_rang_process_action 执行；
 *         SL_STATUS_NOT_READY (capabilities 未读) / SL_STATUS_ALREADY_EXISTS (已在测距)。
 */
sl_status_t cs_key_rang_start(uint8_t conn_handle);

/**
 * 停止 CS 测距 (BLE 连接保持)。用户通过 console 命令触发 (如 "rang stop")。
 * 内部: 查 instance → 清除 start_pending/ranging_active → 标记 teardown_pending
 *       → 调用 cs_initiator_delete 销毁协议栈侧 initiator → 重置测量缓存。
 * @return SL_STATUS_NOT_FOUND (instance 不存在)；SL_STATUS_INVALID_STATE (未在测距)。
 */
sl_status_t cs_key_rang_stop(uint8_t conn_handle);

/**
 * 查询连接是否可开始测距。console status 命令调用。
 * 内部: read_remote_capabilities && conn_interval_ready 均就绪时返回 true。
 */
bool cs_key_rang_is_connection_ready(uint8_t conn_handle);

/**
 * 查询指定连接是否正在测距。console status 命令调用。
 * 内部: 返回 instance 的 ranging_active 标志。
 */
bool cs_key_rang_is_ranging(uint8_t conn_handle);

/**
 * 查询是否有新测量结果。console status 命令周期性查询。
 * 内部: 返回 instance 的 result_updated 标志 (cs_key_rang_get_result 会清除此标志)。
 */
bool cs_key_rang_has_new_result(uint8_t conn_handle);

/**
 * 取出最新测量结果并清除 new-result 标志。外部模块 (GATT/console) 周期性轮询。
 * 内部: 校验 instance → 校验 ranging_active 且有测量计数 → cs_key_rang_fill_result 拷贝 → 清除 flag。
 * @return SL_STATUS_OK / SL_STATUS_NOT_FOUND / SL_STATUS_EMPTY (尚无测量数据)。
 */
sl_status_t cs_key_rang_get_result(uint8_t conn_handle, cs_key_rang_result_t *out);

/**
 * 触发 CS 能力读取。app.c 中注册为 GATT 就绪回调 (key_gatt_comm_set_ready_callback)。
 * 连接建立后 GATT 服务发现完成时调用，作为 connection_parameters 事件之外的兜底路径。
 * 内部: 防重复 (capabilities_read_pending / read_remote_capabilities)
 *       → 调用 sl_bt_cs_read_remote_supported_capabilities。可重复调用，幂等。
 */
void cs_key_rang_trigger_capabilities_read(uint8_t conn_handle);

#ifdef __cplusplus
}
#endif

#endif // CS_KEY_RANG_H
