/***************************************************************************//**
 * @file
 * @brief CS Channel Sounding ranging module implementation
 ******************************************************************************/
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

#include "sl_bluetooth.h"
#include "sl_component_catalog.h"
#include "sl_sleeptimer.h"
#include "app_assert.h"
#include "app_timer.h"
#include "app_config.h"

#include "cs_key_rang.h"
#include "cs_key_rang_config.h"
#include "cs_key_rang_log.h"

#include "cs_antenna.h"
#include "cs_result.h"
#include "cs_initiator.h"
#include "cs_initiator_client.h"
#include "cs_initiator_config.h"
#include "cs_initiator_display_core.h"
#include "cs_initiator_display.h"
#include "cs_ras_client.h"

#include "ble_peer_manager_common.h"
#include "ble_peer_manager_connections.h"


#ifdef SL_CATALOG_CS_INITIATOR_CLI_PRESENT
#include "cs_initiator_cli.h"
#endif


// -----------------------------------------------------------------------------

#define CS_KEY_RANG_ABS(x)  ((x < 0) ? ((-1) * (x)) : (x))

typedef struct {
  float distance_filtered;
  float distance_raw;
  float likeliness;
  float distance_estimate_rssi;
  float velocity;
  float bit_error_rate;
} cs_key_rang_measurement_data_t;

typedef struct {
  uint8_t conn_handle;
  uint32_t measurement_cnt;
  uint32_t ranging_counter;
  cs_key_rang_measurement_data_t measurement_mainmode;
  cs_key_rang_measurement_data_t measurement_submode;
  cs_intermediate_result_t measurement_progress;
  bool measurement_arrived;
  bool measurement_progress_changed;
  bool read_remote_capabilities;
  bool capabilities_read_pending;
  bool ranging_active;
  bool start_pending;
  bool conn_interval_ready;
  bool teardown_pending;
  uint32_t teardown_ready_ms;
  bool result_updated;
  uint8_t number_of_measurements;
  uint8_t retry_count;
} cs_key_rang_instance_t;

// -----------------------------------------------------------------------------

static uint8_t cs_key_rang_get_algo_mode(void);
static const char *cs_key_rang_antenna_usage_to_str(const cs_initiator_config_t *config);
static const char *cs_key_rang_algo_mode_to_str(uint8_t algo_mode);
static void cs_key_rang_on_result(const uint8_t conn_handle,
                              const uint16_t ranging_counter,
                              const uint8_t *result,
                              const cs_result_session_data_t *result_data,
                              const cs_ranging_data_t *ranging_data,
                              const void *user_data);
static void cs_key_rang_on_intermediate_result(const cs_intermediate_result_t *intermediate_result,
                                           const void *user_data);
static void cs_key_rang_on_error(uint8_t conn_handle, cs_error_event_t err_evt, sl_status_t sc);
static sl_status_t cs_key_rang_get_instance_number(uint8_t conn_handle, uint8_t *instance_num);
static void cs_key_rang_check_cli_values(void);
static sl_status_t cs_key_rang_prepare_connection(uint8_t conn_handle);
static sl_status_t cs_key_rang_create_initiator(uint8_t conn_handle);
static void cs_key_rang_delete_instance(uint8_t conn_handle);
static void cs_key_rang_display_timer_callback(app_timer_t *timer, void *data);
static void cs_key_rang_apply_user_config(void);
static void cs_key_rang_apply_mixed_mode_steps(void);
static void cs_key_rang_log_startup_config(void);
static bool cs_key_rang_any_ranging_active(void);
static void cs_key_rang_fill_result(const cs_key_rang_instance_t *inst, cs_key_rang_result_t *out);
static void cs_key_rang_reset_measurement_cache(uint8_t instance_num);
static sl_status_t cs_key_rang_begin_initiator(uint8_t conn_handle, uint8_t instance_num);
static void cs_key_rang_process_pending_operations(void);
static bool cs_key_rang_status_needs_teardown_retry(sl_status_t sc);
static void cs_key_rang_schedule_teardown_retry(uint8_t instance_num);
static bool cs_key_rang_teardown_quiesce_elapsed(uint8_t instance_num);
static void cs_key_rang_on_connection_parameters(uint8_t conn_handle,
                                             uint8_t instance_num,
                                             uint16_t interval,
                                             uint8_t security_mode);

static bool antenna_set_pbr = false;
static bool antenna_set_rtt = false;
static cs_initiator_config_t initiator_config = INITIATOR_CONFIG_DEFAULT;
static rtl_config_t rtl_config = RTL_CONFIG_DEFAULT;
static uint8_t num_reflector_connections = 0u;
static cs_key_rang_instance_t cs_key_rang_instances[CS_INITIATOR_MAX_CONNECTIONS];
static uint32_t last_measurement_log_ms[CS_INITIATOR_MAX_CONNECTIONS];
static uint8_t cs_key_rang_pre_instance_cap_conn = SL_BT_INVALID_CONNECTION_HANDLE;
static uint16_t cs_key_rang_pre_instance_interval = 0u;
static app_timer_t display_timer;

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------

/**
 * 模块初始化。app_init() 启动时调用一次。
 * 内部: 清零全部 instance 状态 → 加载编译期配置 → 混合模式修正 → 打印启动信息 → 启动 display 定时器。
 */
void cs_key_rang_init(void)
{
  for (uint32_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    cs_key_rang_instances[i].conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
    cs_key_rang_instances[i].measurement_cnt = 0u;
    cs_key_rang_instances[i].ranging_counter = 0u;
    memset(&cs_key_rang_instances[i].measurement_mainmode, 0u, sizeof(cs_key_rang_measurement_data_t));
    memset(&cs_key_rang_instances[i].measurement_submode, 0u, sizeof(cs_key_rang_measurement_data_t));
    memset(&cs_key_rang_instances[i].measurement_progress, 0u, sizeof(cs_intermediate_result_t));
    cs_key_rang_instances[i].measurement_arrived = false;
    cs_key_rang_instances[i].measurement_progress_changed = false;
    cs_key_rang_instances[i].read_remote_capabilities = false;
    cs_key_rang_instances[i].capabilities_read_pending = false;
    cs_key_rang_instances[i].ranging_active = false;
    cs_key_rang_instances[i].start_pending = false;
    cs_key_rang_instances[i].conn_interval_ready = false;
    cs_key_rang_instances[i].teardown_pending = false;
    cs_key_rang_instances[i].teardown_ready_ms = 0u;
    cs_key_rang_instances[i].result_updated = false;
    cs_key_rang_instances[i].number_of_measurements = 0u;
    cs_key_rang_instances[i].retry_count = 0u;
    last_measurement_log_ms[i] = 0u;
  }

  cs_key_rang_apply_user_config();
  cs_key_rang_apply_mixed_mode_steps();

  cs_key_rang_log_startup_config();

  sl_status_t sc = cs_initiator_display_init();
  app_assert_status_f(sc, "cs_initiator_display_init failed");
  cs_initiator_display_set_measurement_mode(initiator_config.cs_main_mode, rtl_config.algo_mode);
  app_timer_start(&display_timer,
                  CS_KEY_RANG_DISPLAY_REFRESH_RATE_MS,
                  cs_key_rang_display_timer_callback,
                  NULL,
                  true);
}

/**
 * 获取编译期默认配置。外部(CLI/console)运行时查询。
 * 内部: 将 cs_key_rang_config.h 的宏值填入 *cfg。
 */
void cs_key_rang_get_default_config(cs_key_rang_config_t *cfg)
{
  if (cfg == NULL) {
    return;
  }
  cfg->cs_main_mode = CS_KEY_RANG_CS_MAIN_MODE;
  cfg->cs_sub_mode = CS_KEY_RANG_CS_SUB_MODE;
  cfg->algo_mode = CS_KEY_RANG_ALGO_MODE;
  cfg->channel_map_preset = CS_KEY_RANG_CHANNEL_MAP_PRESET;
  cfg->procedure_scheduling = CS_KEY_RANG_PROCEDURE_SCHEDULING;
  cfg->max_procedure_count = CS_KEY_RANG_MAX_PROCEDURE_COUNT;
}

/**
 * 运行时修改 CS 参数。CLI/console 调用，仅当无活跃测距时允许。
 * 内部: 校验是否可配置 → 将 cfg 写入 initiator_config/rtl_config → 重新应用 channel map 和混合模式。
 */
sl_status_t cs_key_rang_configure(const cs_key_rang_config_t *cfg)
{
  if (cfg == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (!cs_key_rang_is_configurable()) {
    return SL_STATUS_BUSY;
  }

  initiator_config.cs_main_mode = cfg->cs_main_mode;
  initiator_config.cs_sub_mode = cfg->cs_sub_mode;
  initiator_config.channel_map_preset = cfg->channel_map_preset;
  initiator_config.procedure_scheduling = cfg->procedure_scheduling;
  initiator_config.max_procedure_count = cfg->max_procedure_count;
  rtl_config.algo_mode = cfg->algo_mode;

  cs_initiator_apply_channel_map_preset(initiator_config.channel_map_preset,
                                        initiator_config.channel_map.data);
  cs_key_rang_apply_mixed_mode_steps();

  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Runtime configuration applied" CS_KEY_RANG_NL);
  return SL_STATUS_OK;
}

/**
 * 查询是否允许修改配置。cs_key_rang_configure() 和 CLI 的守卫。
 * 内部: 遍历所有 instance，任一 ranging_active 则返回 false。
 */
bool cs_key_rang_is_configurable(void)
{
  return !cs_key_rang_any_ranging_active();
}

/**
 * 启动 CS 测距。用户通过 console 命令触发 (如 "rang start")。
 * 内部: 查 instance → 校验 capabilities 已读 + 连接间隔就绪 → 若 teardown 未完成则排队等待
 *       → 调用 cs_key_rang_begin_initiator 创建 initiator。
 * 返回 SL_STATUS_IN_PROGRESS 表示已排队，cs_key_rang_process_action 稍后执行。
 */
sl_status_t cs_key_rang_start(uint8_t conn_handle)
{
  uint8_t instance_num;
  sl_status_t sc = cs_key_rang_get_instance_number(conn_handle, &instance_num);

  if (sc != SL_STATUS_OK) {
    return SL_STATUS_NOT_FOUND;
  }
  if (!cs_key_rang_instances[instance_num].read_remote_capabilities) {
    return SL_STATUS_NOT_READY;
  }
  if (!cs_key_rang_instances[instance_num].conn_interval_ready) {
    cs_key_rang_instances[instance_num].start_pending = true;
    return SL_STATUS_IN_PROGRESS;
  }
  if (cs_key_rang_instances[instance_num].ranging_active) {
    return SL_STATUS_ALREADY_EXISTS;
  }
  if (cs_key_rang_instances[instance_num].start_pending) {
    return SL_STATUS_IN_PROGRESS;
  }

  if (cs_initiator_has_instance(conn_handle)
      || cs_key_rang_instances[instance_num].teardown_pending) {
    cs_key_rang_instances[instance_num].start_pending = true;
    CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                         "CS ranging start pending (initiator teardown)" CS_KEY_RANG_NL,
                         conn_handle);
    return SL_STATUS_IN_PROGRESS;
  }

  sc = cs_key_rang_begin_initiator(conn_handle, instance_num);
  if (sc == SL_STATUS_IN_PROGRESS) {
    cs_key_rang_instances[instance_num].start_pending = true;
  }
  return sc;
}

/**
 * 停止 CS 测距 (BLE 连接保持)。用户通过 console 命令触发 (如 "rang stop")。
 * 内部: 查 instance → 清除 start_pending/ranging_active → 标记 teardown_pending
 *       → 调用 cs_initiator_delete 销毁协议栈侧 initiator → 重置测量缓存。
 */
sl_status_t cs_key_rang_stop(uint8_t conn_handle)
{
  uint8_t instance_num;
  sl_status_t sc = cs_key_rang_get_instance_number(conn_handle, &instance_num);

  if (sc != SL_STATUS_OK) {
    return SL_STATUS_NOT_FOUND;
  }
  if (!cs_key_rang_instances[instance_num].ranging_active
      && !cs_key_rang_instances[instance_num].start_pending
      && !cs_key_rang_instances[instance_num].teardown_pending
      && !cs_initiator_has_instance(conn_handle)) {
    return SL_STATUS_INVALID_STATE;
  }

  cs_key_rang_instances[instance_num].start_pending = false;
  cs_key_rang_instances[instance_num].ranging_active = false;
  cs_key_rang_instances[instance_num].measurement_arrived = false;
  cs_key_rang_instances[instance_num].measurement_progress_changed = false;
  cs_key_rang_instances[instance_num].teardown_pending = true;
  cs_key_rang_instances[instance_num].teardown_ready_ms = 0u;
  cs_key_rang_reset_measurement_cache(instance_num);

  if (cs_initiator_has_instance(conn_handle)) {
    sc = cs_initiator_delete(conn_handle);
    if ((sc != SL_STATUS_OK) && (sc != SL_STATUS_NOT_FOUND) && (sc != SL_STATUS_INVALID_HANDLE)) {
      return sc;
    }
  }

  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "CS ranging stopped" CS_KEY_RANG_NL, conn_handle);
  return SL_STATUS_OK;
}

/**
 * 查询连接是否可开始测距。console status 命令调用。
 * 内部: 返回 read_remote_capabilities && conn_interval_ready 均就绪。
 */
bool cs_key_rang_is_connection_ready(uint8_t conn_handle)
{
  uint8_t instance_num;
  sl_status_t sc = cs_key_rang_get_instance_number(conn_handle, &instance_num);

  if (sc != SL_STATUS_OK) {
    return false;
  }
  return cs_key_rang_instances[instance_num].read_remote_capabilities
         && cs_key_rang_instances[instance_num].conn_interval_ready;
}

/**
 * 查询指定连接是否正在测距。console status 命令调用。
 * 内部: 返回 instance 的 ranging_active 标志。
 */
bool cs_key_rang_is_ranging(uint8_t conn_handle)
{
  uint8_t instance_num;
  sl_status_t sc = cs_key_rang_get_instance_number(conn_handle, &instance_num);

  if (sc != SL_STATUS_OK) {
    return false;
  }
  return cs_key_rang_instances[instance_num].ranging_active;
}

/**
 * 查询是否有新测量结果。console status 命令周期性查询。
 * 内部: 返回 instance 的 result_updated 标志，cs_key_rang_get_result 会清除。
 */
bool cs_key_rang_has_new_result(uint8_t conn_handle)
{
  uint8_t instance_num;
  sl_status_t sc = cs_key_rang_get_instance_number(conn_handle, &instance_num);

  if (sc != SL_STATUS_OK) {
    return false;
  }
  return cs_key_rang_instances[instance_num].result_updated;
}

/**
 * 取出最新测量结果并清除 new-result 标志。外部模块 (GATT/console) 周期性轮询。
 * 内部: 校验 instance → 校验 ranging_active 且有测量计数 → cs_key_rang_fill_result 拷贝 → 清除 flag。
 */
sl_status_t cs_key_rang_get_result(uint8_t conn_handle, cs_key_rang_result_t *out)
{
  uint8_t instance_num;
  sl_status_t sc = cs_key_rang_get_instance_number(conn_handle, &instance_num);

  if (out == NULL) {
    return SL_STATUS_NULL_POINTER;
  }
  if (sc != SL_STATUS_OK) {
    return SL_STATUS_NOT_FOUND;
  }
  if (!cs_key_rang_instances[instance_num].ranging_active) {
    return SL_STATUS_EMPTY;
  }
  if (cs_key_rang_instances[instance_num].measurement_cnt == 0u) {
    return SL_STATUS_EMPTY;
  }

  cs_key_rang_fill_result(&cs_key_rang_instances[instance_num], out);
  cs_key_rang_instances[instance_num].result_updated = false;
  return SL_STATUS_OK;
}

/**
 * 触发 CS 能力读取。app.c 中注册为 GATT 就绪回调 (key_gatt_comm_set_ready_callback)。
 * 连接建立后 GATT 服务发现完成时调用，作为 connection_parameters 事件之外的兜底路径。
 * 内部: 防重复 (capabilities_read_pending / read_remote_capabilities)
 *       → 调用 sl_bt_cs_read_remote_supported_capabilities。
 */
void cs_key_rang_trigger_capabilities_read(uint8_t conn_handle)
{
  uint8_t instance_num;
  sl_status_t sc;

  if (cs_key_rang_get_instance_number(conn_handle, &instance_num) == SL_STATUS_OK) {
    if (cs_key_rang_instances[instance_num].read_remote_capabilities) {
      return;
    }
    if (cs_key_rang_instances[instance_num].capabilities_read_pending) {
      return;
    }
    cs_key_rang_instances[instance_num].capabilities_read_pending = true;
  } else {
    if (cs_key_rang_pre_instance_cap_conn == conn_handle) {
      return;
    }
    cs_key_rang_pre_instance_cap_conn = conn_handle;
  }

  sc = sl_bt_cs_read_remote_supported_capabilities(conn_handle);
  if (sc != SL_STATUS_OK) {
    if (cs_key_rang_get_instance_number(conn_handle, &instance_num) == SL_STATUS_OK) {
      cs_key_rang_instances[instance_num].capabilities_read_pending = false;
    } else if (cs_key_rang_pre_instance_cap_conn == conn_handle) {
      cs_key_rang_pre_instance_cap_conn = SL_BT_INVALID_CONNECTION_HANDLE;
    }
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                          "Failed to trigger CS capabilities read, sc=0x%lx" CS_KEY_RANG_NL,
                          conn_handle, (unsigned long)sc);
  }
}

/**
 * 主循环轮询。app_process_action() 每 tick 调用。
 * 内部: ① cs_key_rang_process_pending_operations — 处理排队的 start/teardown
 *       ② 遍历活跃 instance: 消费 measurement_arrived → 限频打印 & 更新 display
 *          / 消费 measurement_progress_changed → 打印进度 & 更新 display。
 */
void cs_key_rang_process_action(void)
{
  cs_key_rang_process_pending_operations();

  for (uint8_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (!cs_key_rang_instances[i].ranging_active) {
      continue;
    }
    if (cs_key_rang_instances[i].measurement_arrived) {
      cs_key_rang_instances[i].measurement_arrived = false;

      uint32_t now_ms = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
      bool log_measurement = (last_measurement_log_ms[i] == 0u)
                             || ((uint32_t)(now_ms - last_measurement_log_ms[i])
                                 >= CS_KEY_RANG_MEASUREMENT_LOG_INTERVAL_MS);
      if (log_measurement) {
        last_measurement_log_ms[i] = now_ms;

        CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "# %04lu --- Ranging Counter = %04lu" CS_KEY_RANG_NL,
                                    cs_key_rang_instances[i].conn_handle,
                                    cs_key_rang_instances[i].measurement_cnt,
                                    cs_key_rang_instances[i].ranging_counter);

        const bd_addr *bt_address =
          ble_peer_manager_get_bt_address(cs_key_rang_instances[i].conn_handle);
        CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                    "BT Address: %02X:%02X:%02X:%02X:%02X:%02X" CS_KEY_RANG_NL,
                                    cs_key_rang_instances[i].conn_handle,
                                    bt_address->addr[5],
                                    bt_address->addr[4],
                                    bt_address->addr[3],
                                    bt_address->addr[2],
                                    bt_address->addr[1],
                                    bt_address->addr[0]);

        CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                    "Measurement main mode result: %lu mm" CS_KEY_RANG_NL,
                                    cs_key_rang_instances[i].conn_handle,
                                    (uint32_t)(cs_key_rang_instances[i].measurement_mainmode.distance_filtered
                                               * 1000.f));
        if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
          CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                      "Measurement sub mode result: %lu mm" CS_KEY_RANG_NL,
                                      cs_key_rang_instances[i].conn_handle,
                                      (uint32_t)(cs_key_rang_instances[i].measurement_submode.distance_filtered
                                                 * 1000.f));
        }

        CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                    "Raw main mode distance: %lu mm" CS_KEY_RANG_NL,
                                    cs_key_rang_instances[i].conn_handle,
                                    (uint32_t)(cs_key_rang_instances[i].measurement_mainmode.distance_raw * 1000.f));

        if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
          CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                      "Raw sub mode distance: %lu mm" CS_KEY_RANG_NL,
                                      cs_key_rang_instances[i].conn_handle,
                                      (uint32_t)(cs_key_rang_instances[i].measurement_submode.distance_raw * 1000.f));
        }

        CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                    "Measurement main mode likeliness: %01u.%02u" CS_KEY_RANG_NL,
                                    cs_key_rang_instances[i].conn_handle,
                                    ((uint8_t)cs_key_rang_instances[i].measurement_mainmode.likeliness),
                                    (uint16_t)((uint32_t)(cs_key_rang_instances[i].measurement_mainmode.likeliness
                                                          * 100.f)) % 100);

        if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
          CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                      "Measurement sub mode likeliness: %01u.%02u" CS_KEY_RANG_NL,
                                      cs_key_rang_instances[i].conn_handle,
                                      ((uint8_t)cs_key_rang_instances[i].measurement_submode.likeliness),
                                      (uint16_t)((uint32_t)(cs_key_rang_instances[i].measurement_submode.likeliness
                                                            * 100.f)) % 100);
        }

        CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "RSSI distance: %lu mm" CS_KEY_RANG_NL,
                                    cs_key_rang_instances[i].conn_handle,
                                    (uint32_t)(cs_key_rang_instances[i].measurement_mainmode.distance_estimate_rssi
                                               * 1000.f));

        if (rtl_config.algo_mode == SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST
            && initiator_config.cs_main_mode == sl_bt_cs_mode_pbr
            && (initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_HIGH
                || initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_MEDIUM)) {
          CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Velocity: %s%lu.%02u" CS_KEY_RANG_NL,
                                      cs_key_rang_instances[i].conn_handle,
                                      (cs_key_rang_instances[i].measurement_mainmode.velocity >= 0) ? " " : "-",
                                      ((uint32_t)CS_KEY_RANG_ABS(cs_key_rang_instances[i].measurement_mainmode.velocity)),
                                      (uint16_t)((uint32_t)(CS_KEY_RANG_ABS(cs_key_rang_instances[i].measurement_mainmode.velocity)
                                                            * 100.f + 0.5f)) % 100);
        }
        if ((initiator_config.cs_main_mode == sl_bt_cs_mode_rtt)
            && !isnan(cs_key_rang_instances[i].measurement_mainmode.bit_error_rate)) {
          CS_KEY_RANG_LOG_MEASUREMENT_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "CS bit error rate: %1u.%02u" CS_KEY_RANG_NL,
                                      cs_key_rang_instances[i].conn_handle,
                                      ((uint8_t)cs_key_rang_instances[i].measurement_mainmode.bit_error_rate),
                                      (uint16_t)((uint32_t)(CS_KEY_RANG_ABS(cs_key_rang_instances[i].measurement_mainmode.bit_error_rate)
                                                            * 100.f)) % 100);
        }
      }

      cs_initiator_display_update_data(i,
                                       cs_key_rang_instances[i].conn_handle,
                                       CS_INITIATOR_DISPLAY_STATUS_CONNECTED,
                                       cs_key_rang_instances[i].measurement_mainmode.distance_filtered,
                                       cs_key_rang_instances[i].measurement_mainmode.distance_estimate_rssi,
                                       cs_key_rang_instances[i].measurement_mainmode.likeliness,
                                       cs_key_rang_instances[i].measurement_mainmode.bit_error_rate,
                                       cs_key_rang_instances[i].measurement_mainmode.distance_raw,
                                       cs_key_rang_instances[i].measurement_progress.progress_percentage,
                                       rtl_config.algo_mode,
                                       initiator_config.cs_main_mode);
    } else if (cs_key_rang_instances[i].measurement_progress_changed) {
      cs_key_rang_instances[i].measurement_progress_changed = false;

      CS_KEY_RANG_LOG_PROGRESS_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "# %04lu ---" CS_KEY_RANG_NL,
                               cs_key_rang_instances[i].measurement_progress.connection,
                               cs_key_rang_instances[i].measurement_cnt);

      CS_KEY_RANG_LOG_PROGRESS_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                               "Estimation in progress: %3u.%02u %%" CS_KEY_RANG_NL,
                               cs_key_rang_instances[i].measurement_progress.connection,
                               ((uint8_t)cs_key_rang_instances[i].measurement_progress.progress_percentage),
                               (uint16_t)((uint32_t)(cs_key_rang_instances[i].measurement_progress.progress_percentage
                                                     * 100.f)) % 100);

      cs_initiator_display_update_data(i,
                                       cs_key_rang_instances[i].conn_handle,
                                       CS_INITIATOR_DISPLAY_STATUS_CONNECTED,
                                       cs_key_rang_instances[i].measurement_mainmode.distance_filtered,
                                       cs_key_rang_instances[i].measurement_mainmode.distance_estimate_rssi,
                                       cs_key_rang_instances[i].measurement_mainmode.likeliness,
                                       cs_key_rang_instances[i].measurement_mainmode.bit_error_rate,
                                       cs_key_rang_instances[i].measurement_mainmode.distance_raw,
                                       cs_key_rang_instances[i].measurement_progress.progress_percentage,
                                       rtl_config.algo_mode,
                                       initiator_config.cs_main_mode);
    }
  }
}

/**
 * BLE 协议栈事件分发。app.c 中 sl_bt_on_event() 调用。
 * 处理:
 *   sl_bt_evt_system_boot — 启动时设置 TX power、天线、打印蓝牙地址
 *   sl_bt_evt_connection_parameters — 连接间隔更新 → cs_key_rang_on_connection_parameters；若 instance 不存在则触发 pre-instance 能力读取
 *   sl_bt_evt_gatt_mtu_exchanged — 记录协商后的 MTU
 *   sl_bt_evt_cs_read_remote_supported_capabilities_complete — capabilities 读取完成
 *     → 优化连接/过程间隔 → cs_key_rang_prepare_connection → 请求连接参数更新
 */
void cs_key_rang_on_bt_event(sl_bt_msg_t *evt)
{
  sl_status_t sc;
  uint8_t instance_num;

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_system_boot_id:
    {
      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_PREFIX "=== CS initiator v2 (interval-gated start + retry) ===" CS_KEY_RANG_NL);

      int16_t min_tx_power_x10 = SYSTEM_MIN_TX_POWER_DBM * 10;
      int16_t max_tx_power_x10 = SYSTEM_MAX_TX_POWER_DBM * 10;
      sc = sl_bt_system_set_tx_power(min_tx_power_x10,
                                     max_tx_power_x10,
                                     &min_tx_power_x10,
                                     &max_tx_power_x10);
      app_assert_status(sc);
      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_PREFIX "Min system TX power: %d dBm" CS_KEY_RANG_NL,
                           min_tx_power_x10 / 10);
      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_PREFIX "Max system TX power: %d dBm" CS_KEY_RANG_NL,
                           max_tx_power_x10 / 10);

      sc = cs_antenna_configure(CS_INITIATOR_ANTENNA_OFFSET);
      app_assert_status(sc);

      cs_initiator_init();

      bd_addr address;
      uint8_t address_type;
      sc = sl_bt_gap_get_identity_address(&address, &address_type);
      app_assert_status(sc);
      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_PREFIX "Bluetooth %s address: %02X:%02X:%02X:%02X:%02X:%02X" CS_KEY_RANG_NL,
                           address_type ? "static random" : "public device",
                           address.addr[5], address.addr[4], address.addr[3],
                           address.addr[2], address.addr[1], address.addr[0]);
      break;
    }

    case sl_bt_evt_connection_parameters_id:
    {
      uint8_t conn = evt->data.evt_connection_parameters.connection;

      if (cs_key_rang_get_instance_number(conn, &instance_num) != SL_STATUS_OK) {
        if (evt->data.evt_connection_parameters.security_mode
            != sl_bt_connection_mode1_level1) {
          cs_key_rang_pre_instance_interval = evt->data.evt_connection_parameters.interval;
          if (cs_key_rang_pre_instance_cap_conn != conn) {
            cs_key_rang_pre_instance_cap_conn = conn;
            sc = sl_bt_cs_read_remote_supported_capabilities(conn);
            if (sc != SL_STATUS_OK) {
              cs_key_rang_pre_instance_cap_conn = SL_BT_INVALID_CONNECTION_HANDLE;
              CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                    "CS capabilities read failed, sc=0x%lx" CS_KEY_RANG_NL,
                                    conn, (unsigned long)sc);
            }
          }
        }
      } else {
        cs_key_rang_on_connection_parameters(conn,
                                         instance_num,
                                         evt->data.evt_connection_parameters.interval,
                                         evt->data.evt_connection_parameters.security_mode);
      }
      break;
    }

    case sl_bt_evt_gatt_mtu_exchanged_id:
      initiator_config.mtu = evt->data.evt_gatt_mtu_exchanged.mtu;
      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_PREFIX "MTU set to: %u" CS_KEY_RANG_NL, initiator_config.mtu);
      break;

    case sl_bt_evt_cs_read_remote_supported_capabilities_complete_id: {
      uint16_t proc_interval;
      uint16_t conn_interval;
      uint8_t cs_tone_antenna_config_index_temp = initiator_config.cs_tone_antenna_config_idx;
      uint8_t connection = evt->data.evt_cs_read_remote_supported_capabilities_complete.connection;


      sc = sl_bt_cs_read_local_supported_capabilities(NULL, NULL,
               &initiator_config.num_antennas, NULL, NULL, NULL, NULL, NULL,
               NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL);

      app_assert_status(sc);

      if (initiator_config.max_procedure_count == 0) {
        sc = cs_initiator_get_intervals(initiator_config.cs_main_mode,
                                        initiator_config.cs_sub_mode,
                                        initiator_config.procedure_scheduling,
                                        initiator_config.channel_map_preset,
                                        rtl_config.algo_mode,
                                        initiator_config.cs_tone_antenna_config_idx,
                                        initiator_config.use_real_time_ras_mode,
                                        &conn_interval,
                                        &proc_interval);
        if (sc == SL_STATUS_NOT_SUPPORTED) {
          CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                               "Parameter optimization is not supported with the given input parameters" CS_KEY_RANG_NL,
                               connection);
        } else if (sc == SL_STATUS_IDLE) {
          CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_PREFIX
                               "No optimization - using custom procedure scheduling" CS_KEY_RANG_NL);
        } else if (sc == SL_STATUS_OK) {
          initiator_config.max_connection_interval = initiator_config.min_connection_interval = conn_interval;
          initiator_config.max_procedure_interval = initiator_config.min_procedure_interval = proc_interval;
          CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                               "Optimized parameters for connection interval and procedure interval." CS_KEY_RANG_NL,
                               connection);
        } else {
          CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                "Invalid input, cannot optimize parameters." CS_KEY_RANG_NL,
                                connection);
        }
        float period_ms = initiator_config.max_connection_interval * 1.25f
                          * initiator_config.max_procedure_interval;
        CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                             "Connection interval: %u  Procedure interval: %u  Period: %d ms  Frequency: %u.%03u Hz" CS_KEY_RANG_NL,
                             connection,
                             initiator_config.max_connection_interval,
                             initiator_config.max_procedure_interval,
                             (int)period_ms,
                             (uint16_t)(1000.0f / period_ms),
                             (((uint16_t)(1000000.0f / period_ms)) % 1000));
        initiator_config.cs_tone_antenna_config_idx =
          evt->data.evt_cs_read_remote_supported_capabilities_complete.num_antennas;
      }

      initiator_config.cs_tone_antenna_config_idx = cs_tone_antenna_config_index_temp;

      if (cs_key_rang_pre_instance_cap_conn == connection) {
        cs_key_rang_pre_instance_cap_conn = SL_BT_INVALID_CONNECTION_HANDLE;
      }

      sc = cs_key_rang_prepare_connection(connection);
      if (sc != SL_STATUS_OK) {
        CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                              "Failed to prepare connection, CS ranging unavailable for this link" CS_KEY_RANG_NL,
                              connection);
      } else {
        uint8_t inst_num;

        (void)cs_key_rang_get_instance_number(connection, &inst_num);
        cs_key_rang_instances[inst_num].capabilities_read_pending = false;
        cs_key_rang_instances[inst_num].conn_interval_ready = false;
        cs_key_rang_instances[inst_num].start_pending = true;

        if ((cs_key_rang_pre_instance_interval >= initiator_config.min_connection_interval)
            && (cs_key_rang_pre_instance_interval <= initiator_config.max_connection_interval)) {
          cs_key_rang_instances[inst_num].conn_interval_ready = true;
          CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                               "Connection interval already %u (pre-instance)" CS_KEY_RANG_NL,
                               connection,
                               cs_key_rang_pre_instance_interval);
        }

        sc = sl_bt_connection_set_parameters(connection,
                                             initiator_config.min_connection_interval,
                                             initiator_config.max_connection_interval,
                                             initiator_config.latency,
                                             initiator_config.timeout,
                                             initiator_config.min_ce_length,
                                             initiator_config.max_ce_length);
        if (sc != SL_STATUS_OK) {
          CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                                "Failed to set optimized connection parameters, sc=0x%lx" CS_KEY_RANG_NL,
                                connection, (unsigned long)sc);
        } else {
          CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                               "Requested connection interval %u, waiting for confirmation" CS_KEY_RANG_NL,
                               connection,
                               initiator_config.min_connection_interval);
        }
      }
      break;
    }

    default:
      break;
  }
}

/**
 * Peer Manager 事件分发。app.c 中 peer manager handler 调用。
 * 处理:
 *   ON_CONN_OPENED_CENTRAL — 连接建立 → CLI 值同步 → 设置 display 模式
 *   ON_CONN_CLOSED — 连接断开 → 清理 pre-instance 状态 → 停止 ranging → cs_key_rang_delete_instance
 *   ERROR — 仅打印日志
 */
void cs_key_rang_on_peer_manager_event(ble_peer_manager_evt_type_t *event)
{
  sl_status_t sc;
  bd_addr *address;

  switch (event->evt_id) {
    case BLE_PEER_MANAGER_ON_CONN_OPENED_CENTRAL:
    {
      address = ble_peer_manager_get_bt_address(event->connection_id);
      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                           "Connection opened as central with CS Reflector"
                           " '%02X:%02X:%02X:%02X:%02X:%02X'" CS_KEY_RANG_NL,
                           event->connection_id,
                           address->addr[5],
                           address->addr[4],
                           address->addr[3],
                           address->addr[2],
                           address->addr[1],
                           address->addr[0]);
      cs_key_rang_check_cli_values();
      cs_initiator_display_set_measurement_mode(initiator_config.cs_main_mode, rtl_config.algo_mode);
      break;
    }

    case BLE_PEER_MANAGER_ON_CONN_CLOSED: {
      uint8_t instance_num;

      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Connection closed" CS_KEY_RANG_NL,
                           event->connection_id);
      if (event->connection_id == cs_key_rang_pre_instance_cap_conn) {
        cs_key_rang_pre_instance_cap_conn = SL_BT_INVALID_CONNECTION_HANDLE;
      }
      cs_key_rang_pre_instance_interval = 0u;
      if (cs_key_rang_get_instance_number(event->connection_id, &instance_num) == SL_STATUS_OK) {
        cs_key_rang_instances[instance_num].start_pending = false;
        cs_key_rang_instances[instance_num].conn_interval_ready = false;
        cs_key_rang_instances[instance_num].ranging_active = false;
        cs_key_rang_instances[instance_num].teardown_pending = false;
        cs_key_rang_instances[instance_num].teardown_ready_ms = 0u;
        if (cs_initiator_has_instance(event->connection_id)) {
          sc = cs_initiator_delete(event->connection_id);
          if ((sc == SL_STATUS_NOT_FOUND) || (sc == SL_STATUS_INVALID_HANDLE)) {
            CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Initiator instance not found" CS_KEY_RANG_NL,
                                 event->connection_id);
          } else {
            app_assert_status(sc);
            CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Initiator instance removed" CS_KEY_RANG_NL,
                                 event->connection_id);
          }
        }
      }
      cs_key_rang_delete_instance(event->connection_id);
      break;
    }

    case BLE_PEER_MANAGER_ERROR:
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Peer Manager error" CS_KEY_RANG_NL,
                            event->connection_id);
      break;

    default:
      CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Unhandled Peer Manager event (%u)" CS_KEY_RANG_NL,
                           event->connection_id,
                           event->evt_id);
      break;
  }
}

// -----------------------------------------------------------------------------
// Private
// -----------------------------------------------------------------------------

/**
 * 从编译期宏加载用户配置到 initiator_config / rtl_config。
 * 调用时机: cs_key_rang_init() 启动时。
 */
static void cs_key_rang_apply_user_config(void)
{
  initiator_config.cs_main_mode = CS_KEY_RANG_CS_MAIN_MODE;
  initiator_config.cs_sub_mode = CS_KEY_RANG_CS_SUB_MODE;
  initiator_config.channel_map_preset = CS_KEY_RANG_CHANNEL_MAP_PRESET;
  initiator_config.procedure_scheduling = CS_KEY_RANG_PROCEDURE_SCHEDULING;
  initiator_config.max_procedure_count = CS_KEY_RANG_MAX_PROCEDURE_COUNT;

  rtl_config.algo_mode = cs_key_rang_get_algo_mode();

  cs_initiator_apply_channel_map_preset(initiator_config.channel_map_preset,
                                        initiator_config.channel_map.data);
}

/**
 * 混合模式 (PBR+RTT) 修正: 强制使用高精度 channel map 和固定 step 数。
 * 调用时机: cs_key_rang_init() 启动时 / cs_key_rang_configure() 运行时重配置。
 */
static void cs_key_rang_apply_mixed_mode_steps(void)
{
  if ((initiator_config.cs_main_mode == sl_bt_cs_mode_pbr)
      && (initiator_config.cs_sub_mode == sl_bt_cs_mode_rtt)) {
    initiator_config.min_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
    initiator_config.max_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
    initiator_config.channel_map_preset = CS_CHANNEL_MAP_PRESET_HIGH;
    CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Channel map preset set to high" CS_KEY_RANG_NL);
    cs_initiator_apply_channel_map_preset(initiator_config.channel_map_preset,
                                          initiator_config.channel_map.data);
  }
}

/**
 * 遍历全部 instance，检查是否有任一正在测距。
 * 调用者: cs_key_rang_is_configurable()。
 */
static bool cs_key_rang_any_ranging_active(void)
{
  for (uint8_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_key_rang_instances[i].ranging_active) {
      return true;
    }
  }
  return false;
}

/**
 * 将 instance 的 main/sub 测量数据拷贝到对外结果结构体。
 * 调用者: cs_key_rang_get_result()。
 */
static void cs_key_rang_fill_result(const cs_key_rang_instance_t *inst, cs_key_rang_result_t *out)
{
  const cs_key_rang_measurement_data_t *main = &inst->measurement_mainmode;
  const cs_key_rang_measurement_data_t *sub = &inst->measurement_submode;

  out->distance_filtered_m = main->distance_filtered;
  out->distance_raw_m = main->distance_raw;
  out->likeliness = main->likeliness;
  out->distance_rssi_m = main->distance_estimate_rssi;
  out->velocity_mps = main->velocity;
  out->bit_error_rate = main->bit_error_rate;
  out->sub_distance_filtered_m = sub->distance_filtered;
  out->measurement_count = inst->measurement_cnt;
  out->ranging_counter = (uint16_t)inst->ranging_counter;
}

/**
 * 清零指定 instance 的测量缓存 (计数、结果、flags、last_measurement_log_ms)。
 * 调用者: cs_key_rang_stop() / cs_key_rang_begin_initiator() / cs_key_rang_on_error()。
 */
static void cs_key_rang_reset_measurement_cache(uint8_t instance_num)
{
  cs_key_rang_instances[instance_num].measurement_cnt = 0u;
  cs_key_rang_instances[instance_num].ranging_counter = 0u;
  cs_key_rang_instances[instance_num].result_updated = false;
  cs_key_rang_instances[instance_num].measurement_arrived = false;
  cs_key_rang_instances[instance_num].measurement_progress_changed = false;
  memset(&cs_key_rang_instances[instance_num].measurement_mainmode, 0u,
         sizeof(cs_key_rang_measurement_data_t));
  memset(&cs_key_rang_instances[instance_num].measurement_submode, 0u,
         sizeof(cs_key_rang_measurement_data_t));
  last_measurement_log_ms[instance_num] = 0u;
}

/**
 * 判断 sc 是否需要延时重试 teardown (控制器繁忙 / 命令不允许)。
 * 调用者: cs_key_rang_begin_initiator() / cs_key_rang_on_error()。
 */
static bool cs_key_rang_status_needs_teardown_retry(sl_status_t sc)
{
  return (sc == SL_STATUS_FULL)
         || (sc == SL_STATUS_BUSY)
         || (sc == SL_STATUS_BT_CTRL_COMMAND_DISALLOWED);
}

/**
 * 调度延时重试: 标记 start_pending + teardown_pending，设定静默时间戳。
 * 调用者: cs_key_rang_begin_initiator() / cs_key_rang_on_error()。
 */
static void cs_key_rang_schedule_teardown_retry(uint8_t instance_num)
{
  uint32_t now_ms = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());

  cs_key_rang_instances[instance_num].ranging_active = false;
  cs_key_rang_instances[instance_num].start_pending = true;
  cs_key_rang_instances[instance_num].teardown_pending = true;
  cs_key_rang_instances[instance_num].teardown_ready_ms = now_ms + CS_KEY_RANG_TEARDOWN_QUIESCE_MS;
}

/**
 * 检查 teardown 静默期是否已过 (initiator 实例已删除且等待 ≥ CS_KEY_RANG_TEARDOWN_QUIESCE_MS)。
 * 调用者: cs_key_rang_process_pending_operations()。
 */
static bool cs_key_rang_teardown_quiesce_elapsed(uint8_t instance_num)
{
  if (!cs_key_rang_instances[instance_num].teardown_pending) {
    return true;
  }
  if (cs_initiator_has_instance(cs_key_rang_instances[instance_num].conn_handle)) {
    return false;
  }
  if (cs_key_rang_instances[instance_num].teardown_ready_ms == 0u) {
    uint32_t now_ms = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count());
    cs_key_rang_instances[instance_num].teardown_ready_ms = now_ms + CS_KEY_RANG_TEARDOWN_QUIESCE_MS;
    return false;
  }
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count())
         >= cs_key_rang_instances[instance_num].teardown_ready_ms;
}

/**
 * 收到连接参数更新事件后的处理: 判断 interval 是否落在优化区间内 → 设置 conn_interval_ready。
 * 调用者: cs_key_rang_on_bt_event() 的 sl_bt_evt_connection_parameters 分支。
 */
static void cs_key_rang_on_connection_parameters(uint8_t conn_handle,
                                             uint8_t instance_num,
                                             uint16_t interval,
                                             uint8_t security_mode)
{
  bool ready = (interval >= initiator_config.min_connection_interval)
               && (interval <= initiator_config.max_connection_interval);

  cs_key_rang_instances[instance_num].conn_interval_ready = ready;

  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                       "conn_params: interval=%u sec=%u ready=%u" CS_KEY_RANG_NL,
                       conn_handle,
                       interval,
                       security_mode,
                       ready ? 1u : 0u);
}

/**
 * 创建 initiator 实例并启动 ranging 的最后一步。
 * 调用者: cs_key_rang_start() / cs_key_rang_process_pending_operations()。
 * 内部: cs_key_rang_create_initiator → 若控制器繁忙则 schedule 重试
 *       → 成功则标记 ranging_active、清除 pending/teardown、重置缓存。
 */
static sl_status_t cs_key_rang_begin_initiator(uint8_t conn_handle, uint8_t instance_num)
{
  sl_status_t sc = cs_key_rang_create_initiator(conn_handle);

  if (cs_key_rang_status_needs_teardown_retry(sc)) {
    cs_key_rang_schedule_teardown_retry(instance_num);
    if (cs_initiator_has_instance(conn_handle)) {
      (void)cs_initiator_delete(conn_handle);
    }
    CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                         "CS ranging start deferred (controller busy, sc=0x%lx)" CS_KEY_RANG_NL,
                         conn_handle,
                         (unsigned long)sc);
    return SL_STATUS_IN_PROGRESS;
  }
  if (sc != SL_STATUS_OK) {
    return sc;
  }

  cs_key_rang_instances[instance_num].ranging_active = true;
  cs_key_rang_instances[instance_num].start_pending = false;
  cs_key_rang_instances[instance_num].teardown_pending = false;
  cs_key_rang_instances[instance_num].teardown_ready_ms = 0u;
  cs_key_rang_reset_measurement_cache(instance_num);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "CS ranging started" CS_KEY_RANG_NL, conn_handle);
  return SL_STATUS_OK;
}

/**
 * 处理排队的 start / teardown 操作。
 * 调用者: cs_key_rang_process_action() 每 tick。
 * Pass 1: 遍历 instance → 若 start_pending 且条件满足 (capabilities 已读 + interval 就绪 + 静默期已过)
 *         → cs_key_rang_begin_initiator。
 * Pass 2: 遍历 instance → 若仅 teardown_pending (无 start) 且 initiator 已删除 → 清除 teardown 标志。
 */
static void cs_key_rang_process_pending_operations(void)
{
  for (uint8_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    uint8_t conn_handle = cs_key_rang_instances[i].conn_handle;
    sl_status_t sc;

    if ((conn_handle == SL_BT_INVALID_CONNECTION_HANDLE)
        || !cs_key_rang_instances[i].start_pending
        || cs_key_rang_instances[i].ranging_active) {
      continue;
    }
    if (!cs_key_rang_instances[i].read_remote_capabilities) {
      continue;
    }
    if (!cs_key_rang_instances[i].conn_interval_ready) {
      continue;
    }
    if (!cs_key_rang_teardown_quiesce_elapsed(i)) {
      continue;
    }

    cs_key_rang_instances[i].teardown_pending = false;
    cs_key_rang_instances[i].teardown_ready_ms = 0u;

    sc = cs_key_rang_begin_initiator(conn_handle, i);
    if (sc == SL_STATUS_IN_PROGRESS) {
      continue;
    }
    if (sc != SL_STATUS_OK) {
      cs_key_rang_instances[i].start_pending = false;
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                            "Deferred CS ranging start failed, sc=0x%lx" CS_KEY_RANG_NL,
                            conn_handle,
                            (unsigned long)sc);
    }
  }

  for (uint8_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    uint8_t conn_handle = cs_key_rang_instances[i].conn_handle;

    if ((conn_handle == SL_BT_INVALID_CONNECTION_HANDLE)
        || !cs_key_rang_instances[i].teardown_pending
        || cs_key_rang_instances[i].start_pending
        || cs_key_rang_instances[i].ranging_active) {
      continue;
    }
    if (cs_initiator_has_instance(conn_handle)) {
      continue;
    }
    if (!cs_key_rang_teardown_quiesce_elapsed(i)) {
      continue;
    }
    cs_key_rang_instances[i].teardown_pending = false;
    cs_key_rang_instances[i].teardown_ready_ms = 0u;
  }
}

/**
 * 打印启动配置横幅 (模式、天线、channel map、算法等)。
 * 调用者: cs_key_rang_init() 启动时一次。
 */
static void cs_key_rang_log_startup_config(void)
{
  CS_KEY_RANG_LOG_CONFIG_MSG("+-[CS initiator / cs_key_rang module]--------------------------+" CS_KEY_RANG_NL);
  CS_KEY_RANG_LOG_CONFIG_MSG("+---------------------------------------------------------+" CS_KEY_RANG_NL);

  if (initiator_config.procedure_scheduling != CS_PROCEDURE_SCHEDULING_CUSTOM) {
    CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Using %s based procedure scheduling." CS_KEY_RANG_NL,
                          initiator_config.procedure_scheduling
                          == CS_PROCEDURE_SCHEDULING_OPTIMIZED_FOR_FREQUENCY
                          ? "frequency update" : "energy consumption");
  } else {
    CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Using custom procedure scheduling." CS_KEY_RANG_NL);
  }

  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "%s" CS_KEY_RANG_NL,
                        (initiator_config.max_procedure_count == 0)
                        ? "Free running."
                        : "Start new procedure after one finished.");
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Antenna offset: wire%s" CS_KEY_RANG_NL,
                        CS_INITIATOR_ANTENNA_OFFSET ? "d" : "less");
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Default CS procedure interval: %u" CS_KEY_RANG_NL,
                        initiator_config.min_procedure_interval);
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "CS main mode: %s (%u)" CS_KEY_RANG_NL,
                        (initiator_config.cs_main_mode == sl_bt_cs_mode_pbr) ? "PBR" : "RTT",
                        initiator_config.cs_main_mode);
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "CS sub mode: %s (%u)" CS_KEY_RANG_NL,
                        (initiator_config.cs_sub_mode == sl_bt_cs_submode_disabled) ? "Disabled" : "RTT",
                        initiator_config.cs_sub_mode);
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Requested antenna usage: %s" CS_KEY_RANG_NL,
                        cs_key_rang_antenna_usage_to_str(&initiator_config));
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "Object tracking mode: %s" CS_KEY_RANG_NL,
                        cs_key_rang_algo_mode_to_str(rtl_config.algo_mode));
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "CS channel map preset: %d" CS_KEY_RANG_NL,
                        initiator_config.channel_map_preset);
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX
                         "CS channel map: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X" CS_KEY_RANG_NL,
                         initiator_config.channel_map.data[0],
                         initiator_config.channel_map.data[1],
                         initiator_config.channel_map.data[2],
                         initiator_config.channel_map.data[3],
                         initiator_config.channel_map.data[4],
                         initiator_config.channel_map.data[5],
                         initiator_config.channel_map.data[6],
                         initiator_config.channel_map.data[7],
                         initiator_config.channel_map.data[8],
                         initiator_config.channel_map.data[9]);
  CS_KEY_RANG_LOG_CONFIG_MSG(CS_KEY_RANG_LOG_PREFIX "RSSI reference TX power @ 1m: %d dBm" CS_KEY_RANG_NL,
                        (int)initiator_config.rssi_ref_tx_power);
  CS_KEY_RANG_LOG_CONFIG_MSG("+-------------------------------------------------------+" CS_KEY_RANG_NL);
}

/**
 * Display 定时器回调 (周期性)。app_timer 驱动，刷新 LCD/串口显示。
 * 在 cs_key_rang_init() 中注册，周期 CS_KEY_RANG_DISPLAY_REFRESH_RATE_MS。
 */
static void cs_key_rang_display_timer_callback(app_timer_t *timer, void *data)
{
  (void)timer;
  (void)data;
  cs_initiator_display_update();
}

/**
 * 读取当前算法模式，返回编译期默认。
 * 调用者: cs_key_rang_apply_user_config() 启动时。
 */
static uint8_t cs_key_rang_get_algo_mode(void)
{
  return CS_KEY_RANG_ALGO_MODE;
}

/**
 * 天线配置 → 可读字符串。
 * 调用者: cs_key_rang_log_startup_config()。
 */
static const char *cs_key_rang_antenna_usage_to_str(const cs_initiator_config_t *config)
{
  if (config->cs_main_mode == sl_bt_cs_mode_rtt) {
    switch (config->cs_sync_antenna_req) {
      case CS_SYNC_ANTENNA_1:       return "antenna ID 1";
      case CS_SYNC_ANTENNA_2:       return "antenna ID 2";
      case CS_SYNC_SWITCHING:       return "switch between all antenna IDs";
      default:                      return "unknown";
    }
  } else {
    switch (config->cs_tone_antenna_config_idx_req) {
      case CS_ANTENNA_CONFIG_INDEX_SINGLE_ONLY:     return "single antenna on both sides (1:1)";
      case CS_ANTENNA_CONFIG_INDEX_DUAL_I_SINGLE_R: return "dual antenna initiator & single antenna reflector (2:1)";
      case CS_ANTENNA_CONFIG_INDEX_SINGLE_I_DUAL_R: return "single antenna initiator & dual antenna reflector (1:2)";
      case CS_ANTENNA_CONFIG_INDEX_DUAL_ONLY:       return "dual antennas on both sides (2:2)";
      default:                                      return "unknown";
    }
  }
}

/**
 * 算法模式枚举 → 可读字符串。
 * 调用者: cs_key_rang_log_startup_config()。
 */
static const char *cs_key_rang_algo_mode_to_str(uint8_t algo_mode)
{
  switch (algo_mode) {
    case SL_RTL_CS_ALGO_MODE_REAL_TIME_BASIC:       return "real time basic (moving)";
    case SL_RTL_CS_ALGO_MODE_STATIC_HIGH_ACCURACY:  return "stationary object tracking";
    case SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST:        return "real time fast (moving)";
    default:                                        return "unknown";
  }
}

/**
 * 通过 conn_handle 查找对应的 instance 数组下标。
 * 被本模块几乎所有函数调用。
 */
static sl_status_t cs_key_rang_get_instance_number(uint8_t conn_handle, uint8_t *instance_num)
{
  for (uint8_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_key_rang_instances[i].conn_handle == conn_handle) {
      *instance_num = i;
      return SL_STATUS_OK;
    }
  }
  return SL_STATUS_FAIL;
}

/**
 * CS 测量结果回调。由 cs_initiator 层在每次测量完成时调用 (通过 cs_initiator_create 注册)。
 * 内部: 从 result_data 提取 distance/likeliness/velocity/BER/RSSI 等字段
 *       → 存入对应 instance → 置 measurement_arrived + result_updated。
 */
static void cs_key_rang_on_result(const uint8_t conn_handle,
                              const uint16_t ranging_counter,
                              const uint8_t *result,
                              const cs_result_session_data_t *result_data,
                              const cs_ranging_data_t *ranging_data,
                              const void *user_data)
{
  (void)ranging_data;
  (void)user_data;
  uint8_t initiator_num;

  if (result == NULL) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Null result reference!" CS_KEY_RANG_NL, conn_handle);
    return;
  }

  sl_status_t sc = cs_key_rang_get_instance_number(conn_handle, &initiator_num);
  if (sc != SL_STATUS_OK) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                          "Failed to get instance number for connection! [sc: 0x%lx]" CS_KEY_RANG_NL,
                          conn_handle,
                          (unsigned long)sc);
    return;
  }

  sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                               CS_RESULT_FIELD_DISTANCE_MAINMODE,
                               (uint8_t *)result,
                               (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_mainmode.distance_filtered);
  if (sc != SL_STATUS_OK) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Failed to extract distance! [sc: 0x%lx]" CS_KEY_RANG_NL,
                          conn_handle,
                          (unsigned long)sc);
  }

  if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_DISTANCE_SUBMODE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_submode.distance_filtered);
    if (sc != SL_STATUS_OK) {
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                            "Failed to extract sub mode distance! [sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            (unsigned long)sc);
    }
  }

  sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                               CS_RESULT_FIELD_DISTANCE_RAW_MAINMODE,
                               (uint8_t *)result,
                               (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_mainmode.distance_raw);
  if (sc != SL_STATUS_OK) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Failed to extract RAW distance! [sc: 0x%lx]" CS_KEY_RANG_NL,
                          conn_handle,
                          (unsigned long)sc);
  }

  if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_DISTANCE_RAW_SUBMODE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_submode.distance_raw);
    if (sc != SL_STATUS_OK) {
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                            "Failed to extract sub mode RAW distance! [sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            (unsigned long)sc);
    }
  }

  sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                               CS_RESULT_FIELD_LIKELINESS_MAINMODE,
                               (uint8_t *)result,
                               (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_mainmode.likeliness);
  if (sc != SL_STATUS_OK) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Failed to extract likeliness! [sc: 0x%lx]" CS_KEY_RANG_NL,
                          conn_handle,
                          (unsigned long)sc);
  }

  if (initiator_config.cs_sub_mode != sl_bt_cs_submode_disabled) {
    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_LIKELINESS_SUBMODE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_submode.likeliness);
    if (sc != SL_STATUS_OK) {
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                            "Failed to extract sub mode likeliness! [sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            (unsigned long)sc);
    }
  }

  if (rtl_config.algo_mode == SL_RTL_CS_ALGO_MODE_REAL_TIME_FAST
      && initiator_config.cs_main_mode == sl_bt_cs_mode_pbr
      && (initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_HIGH
          || initiator_config.channel_map_preset == CS_CHANNEL_MAP_PRESET_MEDIUM)) {
    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_VELOCITY_MAINMODE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_mainmode.velocity);
    if (sc != SL_STATUS_OK) {
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Failed to extract velocity! [sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            (unsigned long)sc);
    }
  }

  if (initiator_config.cs_main_mode == sl_bt_cs_mode_rtt) {
    sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                                 CS_RESULT_FIELD_BIT_ERROR_RATE,
                                 (uint8_t *)result,
                                 (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_mainmode.bit_error_rate);
    if (sc != SL_STATUS_OK) {
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Failed to extract BER! [sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            (unsigned long)sc);
    }
  }

  sc = cs_result_extract_field((cs_result_session_data_t *)result_data,
                               CS_RESULT_FIELD_DISTANCE_RSSI,
                               (uint8_t *)result,
                               (uint8_t *)&cs_key_rang_instances[initiator_num].measurement_mainmode.distance_estimate_rssi);
  if (sc != SL_STATUS_OK) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Failed to extract RSSI distance! [sc: 0x%lx]" CS_KEY_RANG_NL,
                          conn_handle,
                          (unsigned long)sc);
  }

  cs_key_rang_instances[initiator_num].measurement_arrived = true;
  cs_key_rang_instances[initiator_num].result_updated = true;
  cs_key_rang_instances[initiator_num].measurement_cnt++;
  cs_key_rang_instances[initiator_num].ranging_counter = ranging_counter;
}

/**
 * CS 中间进度回调。由 cs_initiator 层在 stationary 模式下周期性调用。
 * 内部: 将 intermediate_result 拷贝到 instance → 置 measurement_progress_changed。
 */
static void cs_key_rang_on_intermediate_result(const cs_intermediate_result_t *intermediate_result,
                                           const void *user_data)
{
  (void)user_data;
  uint8_t instance_num;

  if (intermediate_result == NULL) {
    return;
  }

  sl_status_t sc = cs_key_rang_get_instance_number(intermediate_result->connection, &instance_num);
  if (sc != SL_STATUS_OK) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                          "Failed to get instance number for connection" CS_KEY_RANG_NL,
                          intermediate_result->connection);
    return;
  }

  memcpy(&cs_key_rang_instances[instance_num].measurement_progress,
         intermediate_result,
         sizeof(cs_intermediate_result_t));
  cs_key_rang_instances[instance_num].measurement_progress_changed = true;
}

/**
 * 从 CLI 模块同步天线/模式/算法等配置到 initiator_config/rtl_config。
 * 调用者: cs_key_rang_on_peer_manager_event() 的 ON_CONN_OPENED_CENTRAL 分支 (连接建立时)。
 */
static void cs_key_rang_check_cli_values(void)
{
#ifdef SL_CATALOG_CS_INITIATOR_CLI_PRESENT
  if (cs_initiator_cli_get_antenna_config_index() != initiator_config.cs_tone_antenna_config_idx_req) {
    antenna_set_pbr = true;
  }
  initiator_config.cs_tone_antenna_config_idx_req = cs_initiator_cli_get_antenna_config_index();
  if (cs_initiator_cli_get_cs_sync_antenna_usage() != initiator_config.cs_sync_antenna_req) {
    antenna_set_rtt = true;
  }
  initiator_config.cs_sub_mode = cs_initiator_cli_get_sub_mode();
  if (initiator_config.cs_sub_mode == sl_bt_cs_submode_disabled) {
    initiator_config.min_main_mode_steps = CS_INITIATOR_DEFAULT_MIN_MAIN_MODE_STEPS;
    initiator_config.max_main_mode_steps = CS_INITIATOR_DEFAULT_MAX_MAIN_MODE_STEPS;
  } else {
    initiator_config.min_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
    initiator_config.max_main_mode_steps = CS_INITIATOR_MIXED_MODE_MAIN_MODE_STEPS;
  }
  initiator_config.cs_sync_antenna_req = cs_initiator_cli_get_cs_sync_antenna_usage();
  initiator_config.cs_main_mode = cs_initiator_cli_get_mode();
  initiator_config.conn_phy = cs_initiator_cli_get_conn_phy();
  rtl_config.algo_mode = cs_initiator_cli_get_algo_mode();
  initiator_config.channel_map_preset = cs_initiator_cli_get_preset();
  cs_initiator_apply_channel_map_preset(initiator_config.channel_map_preset,
                                        initiator_config.channel_map.data);
#endif
}

/**
 * 为连接准备 CS 测距上下文: 若 instance 已存在则标记 capabilities 已读；
 * 否则在 cs_key_rang_instances 数组中分配空闲槽位并初始化。
 * 调用者: cs_key_rang_on_bt_event() 的 capabilities 读取完成分支。
 */
static sl_status_t cs_key_rang_prepare_connection(uint8_t conn_handle)
{
  uint8_t instance_num;

  if (cs_key_rang_get_instance_number(conn_handle, &instance_num) == SL_STATUS_OK) {
    cs_key_rang_instances[instance_num].read_remote_capabilities = true;
    return SL_STATUS_OK;
  }

  if (num_reflector_connections >= CS_INITIATOR_MAX_CONNECTIONS) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_PREFIX "Maximum number of connections (%u) reached" CS_KEY_RANG_NL,
                          CS_INITIATOR_MAX_CONNECTIONS);
    return SL_STATUS_FULL;
  }

  for (uint32_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_key_rang_instances[i].conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
      cs_intermediate_result_t measurement_progress;

      cs_key_rang_instances[i].conn_handle = conn_handle;
      cs_key_rang_instances[i].measurement_cnt = 0u;
      cs_key_rang_instances[i].ranging_counter = 0u;
      memset(&cs_key_rang_instances[i].measurement_mainmode, 0u, sizeof(cs_key_rang_measurement_data_t));
      memset(&cs_key_rang_instances[i].measurement_submode, 0u, sizeof(cs_key_rang_measurement_data_t));
      memset(&cs_key_rang_instances[i].measurement_progress, 0u, sizeof(measurement_progress));
      cs_key_rang_instances[i].measurement_arrived = false;
      cs_key_rang_instances[i].measurement_progress_changed = false;
      cs_key_rang_instances[i].read_remote_capabilities = true;
      cs_key_rang_instances[i].ranging_active = false;
      cs_key_rang_instances[i].start_pending = false;
      cs_key_rang_instances[i].conn_interval_ready = false;
      cs_key_rang_instances[i].teardown_pending = false;
      cs_key_rang_instances[i].teardown_ready_ms = 0u;
      cs_key_rang_instances[i].result_updated = false;
      cs_key_rang_instances[i].retry_count = 0u;
      last_measurement_log_ms[i] = 0u;
      num_reflector_connections++;
      return SL_STATUS_OK;
    }
  }

  return SL_STATUS_FULL;
}

/**
 * 创建协议栈侧 initiator 实例。打印完整配置 dump → 调用 cs_initiator_create 注册回调。
 * 调用者: cs_key_rang_begin_initiator()。
 */
static sl_status_t cs_key_rang_create_initiator(uint8_t conn_handle)
{
  sl_status_t sc;

  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "=== initiator_config dump before cs_initiator_create ===" CS_KEY_RANG_NL,
                   conn_handle);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  num_antennas=%u" CS_KEY_RANG_NL, conn_handle,
                   initiator_config.num_antennas);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  cs_tone_antenna_config_idx=%u  _req=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.cs_tone_antenna_config_idx,
                   initiator_config.cs_tone_antenna_config_idx_req);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  cs_main_mode=%u  cs_sub_mode=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.cs_main_mode, initiator_config.cs_sub_mode);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  conn_interval=[%u,%u]  proc_interval=[%u,%u]" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.min_connection_interval,
                   initiator_config.max_connection_interval,
                   initiator_config.min_procedure_interval,
                   initiator_config.max_procedure_interval);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  main_mode_steps=[%u,%u]  repetition=%u  mode0=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.min_main_mode_steps,
                   initiator_config.max_main_mode_steps,
                   initiator_config.main_mode_repetition,
                   initiator_config.mode0_step);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  ch3c_jump=%u  ch3c_shape=%u  ch_sel=%u  rtt_type=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.ch3c_jump, initiator_config.ch3c_shape,
                   initiator_config.channel_selection_type, initiator_config.rtt_type);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  ch_map_repeat=%u  cs_sync_phy=%u  conn_phy=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.channel_map_repetition,
                   initiator_config.cs_sync_phy, initiator_config.conn_phy);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  max_proc_count=%u  max_proc_dur=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.max_procedure_count,
                   initiator_config.max_procedure_duration);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  subevent_len=[%lu,%lu]  tx_pwr_delta=%d  max_tx=%d" CS_KEY_RANG_NL,
                   conn_handle, (unsigned long)initiator_config.min_subevent_len,
                   (unsigned long)initiator_config.max_subevent_len,
                   (int)initiator_config.tx_pwr_delta,
                   (int)initiator_config.max_tx_power_dbm);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  create_ctx=%u  config_id=%u  pref_ant=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.create_context,
                   initiator_config.config_id, initiator_config.preferred_peer_antenna);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  latency=%u  timeout=%u  mtu=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.latency, initiator_config.timeout,
                   initiator_config.mtu);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  ce_length=[%u,%u]" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.min_ce_length,
                   initiator_config.max_ce_length);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  snr_ctrl=[%u,%u]  real_time_ras=%u  ch_map_preset=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.snr_control_initiator,
                   initiator_config.snr_control_reflector,
                   initiator_config.use_real_time_ras_mode,
                   initiator_config.channel_map_preset);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  cs_sync_antenna=%u  cs_sync_antenna_req=%u" CS_KEY_RANG_NL,
                   conn_handle, initiator_config.cs_sync_antenna,
                   initiator_config.cs_sync_antenna_req);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  rtl: algo_mode=%u  rtl_log=%u" CS_KEY_RANG_NL,
                   conn_handle, rtl_config.algo_mode, rtl_config.rtl_logging_enabled);
  CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "  channel_map: %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X" CS_KEY_RANG_NL,
                   conn_handle,
                   initiator_config.channel_map.data[0], initiator_config.channel_map.data[1],
                   initiator_config.channel_map.data[2], initiator_config.channel_map.data[3],
                   initiator_config.channel_map.data[4], initiator_config.channel_map.data[5],
                   initiator_config.channel_map.data[6], initiator_config.channel_map.data[7],
                   initiator_config.channel_map.data[8], initiator_config.channel_map.data[9]);

  sc = cs_initiator_create(conn_handle,
                           &initiator_config,
                           &rtl_config,
                           cs_key_rang_on_result,
                           cs_key_rang_on_intermediate_result,
                           cs_key_rang_on_error,
                           NULL);
  if (sc != SL_STATUS_OK) {
    CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                          "Failed to create initiator instance, error:0x%lx" CS_KEY_RANG_NL,
                          conn_handle,
                          (unsigned long)sc);
  }
  return sc;
}

/**
 * 从 cs_key_rang_instances 数组中删除指定连接，清零所有字段并递减连接计数。
 * 调用者: cs_key_rang_on_peer_manager_event() 的 ON_CONN_CLOSED 分支 (连接断开时)。
 */
static void cs_key_rang_delete_instance(uint8_t conn_handle)
{
  for (uint32_t i = 0u; i < CS_INITIATOR_MAX_CONNECTIONS; i++) {
    if (cs_key_rang_instances[i].conn_handle == conn_handle) {
      cs_key_rang_instances[i].conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
      cs_key_rang_instances[i].measurement_cnt = 0u;
      memset(&cs_key_rang_instances[i].measurement_mainmode, 0u, sizeof(cs_key_rang_measurement_data_t));
      memset(&cs_key_rang_instances[i].measurement_submode, 0u, sizeof(cs_key_rang_measurement_data_t));
      memset(&cs_key_rang_instances[i].measurement_progress, 0u, sizeof(cs_intermediate_result_t));
      cs_key_rang_instances[i].measurement_arrived = false;
      cs_key_rang_instances[i].measurement_progress_changed = false;
      cs_key_rang_instances[i].read_remote_capabilities = false;
      cs_key_rang_instances[i].capabilities_read_pending = false;
      cs_key_rang_instances[i].ranging_active = false;
      cs_key_rang_instances[i].start_pending = false;
      cs_key_rang_instances[i].conn_interval_ready = false;
      cs_key_rang_instances[i].teardown_pending = false;
      cs_key_rang_instances[i].teardown_ready_ms = 0u;
      cs_key_rang_instances[i].result_updated = false;
      last_measurement_log_ms[i] = 0u;
      num_reflector_connections--;
      break;
    }
  }
}

/**
 * CS 错误回调。由 cs_initiator 层在发生错误时调用 (通过 cs_initiator_create 注册)。
 * 内部: 按 err_evt 分类处理 —
 *       不可恢复 (CS_ERROR_EVENT_CS_PROCEDURE_STOP_TIMER_FAILED / UNEXPECTED_DATA) → app_assert；
 *       RTL 处理错误 → 仅打印；
 *       天线不支持 → 打印警告后继续；
 *       可恢复错误 (RTL init 失败 / 安全升级失败 / 参数设置失败) → 最多重试 2 次，重新读取 capabilities；
 *       其他 → 停止 ranging、重置缓存，必要时调度 teardown 重试。
 */
static void cs_key_rang_on_error(uint8_t conn_handle, cs_error_event_t err_evt, sl_status_t sc)
{
  switch (err_evt) {
    case CS_ERROR_EVENT_CS_PROCEDURE_STOP_TIMER_FAILED:
    case CS_ERROR_EVENT_CS_PROCEDURE_UNEXPECTED_DATA:
      app_assert(false,
                 CS_KEY_RANG_LOG_INSTANCE_PREFIX "Unrecoverable CS procedure error happened!"
                 "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                 conn_handle,
                 err_evt,
                 (unsigned long)sc);
      break;

    case CS_ERROR_EVENT_RTL_PROCESS_ERROR:
      // CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "RTL processing error happened!"
      //                       "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
      //                       conn_handle,
      //                       err_evt,
      //                       (unsigned long)sc);
      break;

    case CS_ERROR_EVENT_INITIATOR_FAILED_TO_SET_INTERVALS:
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Failed to set CS procedure scheduling!"
                            "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            err_evt,
                            (unsigned long)sc);
      break;

    case CS_ERROR_EVENT_INITIATOR_PBR_ANTENNA_USAGE_NOT_SUPPORTED:
      if (antenna_set_pbr) {
        CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                              "The requested PBR antenna configuration is not supported!"
                              " Will use the closest one and continue."
                              "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                              conn_handle,
                              err_evt,
                              (unsigned long)sc);
      } else {
        CS_KEY_RANG_LOG_WARN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                             "Default PBR antenna configuration not supported!"
                             " Will use the closest one and continue."
                             "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                             conn_handle,
                             err_evt,
                             (unsigned long)sc);
      }
      break;

    case CS_ERROR_EVENT_INITIATOR_RTT_ANTENNA_USAGE_NOT_SUPPORTED:
      if (antenna_set_rtt) {
        CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                              "The requested RTT antenna configuration is not supported!"
                              " Will use the closest one and continue."
                              "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                              conn_handle,
                              err_evt,
                              (unsigned long)sc);
      } else {
        CS_KEY_RANG_LOG_WARN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                             "Default RTT antenna configuration not supported!"
                             " Will use the closest one and continue."
                             "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                             conn_handle,
                             err_evt,
                             (unsigned long)sc);
      }
      break;

    case CS_ERROR_EVENT_INITIATOR_FAILED_TO_INIT_RTL_LIB:
    case CS_ERROR_EVENT_INITIATOR_FAILED_TO_ENABLE_CS_SECURITY:
    case CS_ERROR_EVENT_CS_SET_PROCEDURE_PARAMETERS_FAILED: {
      uint8_t instance_num;
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Recoverable error, attempting retry"
                            "[E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            err_evt,
                            (unsigned long)sc);
      if (cs_key_rang_get_instance_number(conn_handle, &instance_num) == SL_STATUS_OK) {
        if (cs_key_rang_instances[instance_num].retry_count < 2u) {
          cs_key_rang_instances[instance_num].retry_count++;
          cs_key_rang_instances[instance_num].ranging_active = false;
          cs_key_rang_instances[instance_num].start_pending = false;
          cs_key_rang_instances[instance_num].conn_interval_ready = false;
          cs_key_rang_instances[instance_num].teardown_pending = false;
          cs_key_rang_instances[instance_num].teardown_ready_ms = 0u;
          cs_key_rang_reset_measurement_cache(instance_num);
          if (cs_initiator_has_instance(conn_handle)) {
            (void)cs_initiator_delete(conn_handle);
          }
          cs_key_rang_instances[instance_num].read_remote_capabilities = false;
          cs_key_rang_instances[instance_num].capabilities_read_pending = true;
          CS_KEY_RANG_LOG_CONN_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX
                               "Retry %u/2: re-reading CS capabilities" CS_KEY_RANG_NL,
                               conn_handle,
                               cs_key_rang_instances[instance_num].retry_count);
          sl_bt_cs_read_remote_supported_capabilities(conn_handle);
          break;
        }
      }
      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Retries exhausted, falling back to default" CS_KEY_RANG_NL,
                            conn_handle);
    }
    /* fall through */
    default: {
      uint8_t instance_num;

      CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "CS ranging error [E: 0x%x sc: 0x%lx]" CS_KEY_RANG_NL,
                            conn_handle,
                            err_evt,
                            (unsigned long)sc);
      if (err_evt == CS_ERROR_EVENT_TIMER_ELAPSED) {
        CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Operation timeout." CS_KEY_RANG_NL, conn_handle);
      } else if (err_evt == CS_ERROR_EVENT_INITIATOR_FAILED_TO_INCREASE_SECURITY) {
        CS_KEY_RANG_LOG_ERROR_MSG(CS_KEY_RANG_LOG_INSTANCE_PREFIX "Security level increase failed." CS_KEY_RANG_NL,
                              conn_handle);
      }
      if (cs_key_rang_get_instance_number(conn_handle, &instance_num) == SL_STATUS_OK) {
        cs_key_rang_instances[instance_num].ranging_active = false;
        cs_key_rang_reset_measurement_cache(instance_num);
        if (cs_key_rang_status_needs_teardown_retry(sc)) {
          cs_key_rang_schedule_teardown_retry(instance_num);
        } else {
          cs_key_rang_instances[instance_num].start_pending = false;
          cs_key_rang_instances[instance_num].teardown_pending = true;
          cs_key_rang_instances[instance_num].teardown_ready_ms = 0u;
        }
      }
      if (cs_initiator_has_instance(conn_handle)) {
        (void)cs_initiator_delete(conn_handle);
      } else {
        (void)cs_ras_client_remove(conn_handle);
      }
      break;
    }
  }
}
