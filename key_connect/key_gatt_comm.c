#include "key_gatt_comm.h"
#include "app.h"
#include "user_log_console.h"
#include <string.h>

// 反射器自定义服务 UUID: db86d78f-61f5-49e8-98b4-2a2e8e217903 (全字节逆序)
static const uint8_t UUID_SERVICE[] = {
  0x03, 0x79, 0x21, 0x8E, 0x2E, 0x2A, 0xB4, 0x98,
  0xE8, 0x49, 0xF5, 0x61, 0x8F, 0xD7, 0x86, 0xDB
};

// gatt_rx: 95221a3f-c6da-491a-b076-0429238d26cf (Write Without Response)
static const uint8_t UUID_CHAR_RX[] = {
  0xCF, 0x26, 0x8D, 0x23, 0x29, 0x04, 0x76, 0xB0,
  0x1A, 0x49, 0xDA, 0xC6, 0x3F, 0x1A, 0x22, 0x95
};

// gatt_tx: db791eff-dfc5-4714-88b2-3db1b07d60f4 (Notify)
static const uint8_t UUID_CHAR_TX[] = {
  0xF4, 0x60, 0x7D, 0xB0, 0xB1, 0x3D, 0xB2, 0x88,
  0x14, 0x47, 0xC5, 0xDF, 0xFF, 0x1E, 0x79, 0xDB
};

typedef enum {
  KGATT_IDLE,
  KGATT_DISCOVER_SERVICES,
  KGATT_DISCOVER_CHARACTERISTICS,
  KGATT_ENABLE_CCCD,
  KGATT_READY,
} kgatt_state_t;

static kgatt_state_t kgatt_state = KGATT_IDLE;
static uint32_t kgatt_service_handle = 0;
static uint16_t kgatt_char_rx_handle = 0;
static uint16_t kgatt_char_tx_handle = 0;
static key_gatt_comm_rx_callback_t kgatt_rx_callback = NULL;
static key_gatt_comm_ready_cb_t kgatt_ready_callback = NULL;

/**
 * 启动 GATT 服务发现流程。状态机入口: IDLE → DISCOVER_SERVICES。
 * 调用者: key_gatt_comm_on_bt_event (sm_bonded / connection_parameters 加密检查)
 *         / key_gatt_comm_process_action (安全网兜底)。
 */
static void kgatt_start_discovery(void)
{
  uint8_t conn = app_key_conn_handle;
  if (conn == SL_BT_INVALID_CONNECTION_HANDLE) return;

  kgatt_state = KGATT_DISCOVER_SERVICES;
  USER_LOG_INFO("[KGATT] start service discovery conn=%u" USER_LOG_NL, conn);
  sl_bt_gatt_discover_primary_services_by_uuid(conn, sizeof(UUID_SERVICE), UUID_SERVICE);
}

/**
 * 模块初始化 (重置状态为 IDLE)。当前未被外部显式调用 (static 变量默认 IDLE)。
 * 保留用于未来显式复位场景。
 */
void key_gatt_comm_init(void)
{
  kgatt_state = KGATT_IDLE;
  kgatt_service_handle = 0;
  kgatt_char_rx_handle = 0;
  kgatt_char_tx_handle = 0;
  kgatt_rx_callback = NULL;
}

/**
 * 注册数据接收回调。key_gatt_cmd_init() 启动时调用。
 * 内部: 将 callback 存入模块静态变量 kgatt_rx_callback。
 */
void key_gatt_comm_set_rx_callback(key_gatt_comm_rx_callback_t callback)
{
  kgatt_rx_callback = callback;
}

/**
 * 注册 GATT 就绪回调。app.c init 阶段调用，传入 cs_key_rang_trigger_capabilities_read。
 * 内部: 将 cb 存入模块静态变量 kgatt_ready_callback。当状态机到达 READY 时触发。
 */
void key_gatt_comm_set_ready_callback(key_gatt_comm_ready_cb_t cb)
{
  kgatt_ready_callback = cb;
}

/**
 * 通过 GATT Write Without Response 向反射器发送数据。key_gatt_cmd 模块调用。
 * 内部: 校验 KGATT_READY + 特征 handle 存在 → sl_bt_gatt_write_characteristic_value_without_response。
 */
sl_status_t key_gatt_comm_send(uint8_t conn, const uint8_t *data, uint8_t len)
{
  if (kgatt_state != KGATT_READY) {
    USER_LOG_ERROR("[KGATT] send: not ready (state=%d)" USER_LOG_NL, kgatt_state);
    return SL_STATUS_NOT_READY;
  }
  if (kgatt_char_rx_handle == 0) {
    USER_LOG_ERROR("[KGATT] send: rx characteristic not found" USER_LOG_NL);
    return SL_STATUS_NOT_FOUND;
  }

  uint16_t sent_len = 0;
  sl_status_t sc = sl_bt_gatt_write_characteristic_value_without_response(
      conn, kgatt_char_rx_handle, len, data, &sent_len);
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[KGATT] tx send FAIL 0x%04lx conn=%u" USER_LOG_NL,
                   (unsigned long)sc, conn);
  }
  return sc;
}

/**
 * BLE 事件分发，驱动 GATT 状态机。sl_bt_on_event() 调用。
 * 处理流程:
 *   connection_closed → 断线复位到 IDLE
 *   sm_bonded (加密完成) → 启动 service discovery
 *   connection_parameters (重连) → 检查已加密 → 启动 discovery
 *   gatt_service → 记录 service handle (UUID 匹配由协议栈保证)
 *   gatt_characteristic → 按 UUID 匹配并记录 RX/TX handle
 *   gatt_procedure_completed → 按当前状态推进: services→chars→cccd→ready (触发 ready_callback)
 *   gatt_characteristic_value (Notify) → 调用 rx_callback 上抛数据
 */
void key_gatt_comm_on_bt_event(sl_bt_msg_t *evt)
{
  uint32_t msg_id = SL_BT_MSG_ID(evt->header);

  // --- 断线重置（任何连接断开都强制回 IDLE，避免漏掉新连接） ---
  if (msg_id == sl_bt_evt_connection_closed_id) {
    USER_LOG_INFO("[KGATT] connection closed conn=%u, force reset to IDLE" USER_LOG_NL,
                  evt->data.evt_connection_closed.connection);
    kgatt_state = KGATT_IDLE;
    kgatt_service_handle = 0;
    kgatt_char_rx_handle = 0;
    kgatt_char_tx_handle = 0;
    return;
  }

  // --- 加密完成触发 ---
  if (msg_id == sl_bt_evt_sm_bonded_id) {
    // 无条件强制重新发现 GATT：新连接必须重新启用 CCCD
    kgatt_state = KGATT_IDLE;
    kgatt_service_handle = 0;
    kgatt_char_rx_handle = 0;
    kgatt_char_tx_handle = 0;
    USER_LOG_INFO("[KGATT] bonded conn=%u, restarting GATT discovery" USER_LOG_NL,
                  evt->data.evt_sm_bonded.connection);
    kgatt_start_discovery();
    return;
  }

  if (msg_id == sl_bt_evt_connection_parameters_id) {
    // 重连场景：如果加密已就绪且 GATT 未发现，启动发现
    uint8_t conn = app_key_conn_handle;
    if (conn != SL_BT_INVALID_CONNECTION_HANDLE && kgatt_state == KGATT_IDLE) {
      uint8_t sec_mode = 0, key_size = 0, bond_h = 0;
      sl_status_t sc = sl_bt_connection_get_security_status(conn, &sec_mode, &key_size, &bond_h);
      if (sc == SL_STATUS_OK && sec_mode > (uint8_t)sl_bt_connection_mode1_level1) {
        USER_LOG_INFO("[KGATT] reconnection encrypted, starting GATT discovery" USER_LOG_NL);
        kgatt_start_discovery();
      }
    }
    return;
  }

  // --- GATT 发现（仅本模块的连接） ---
  if (kgatt_state == KGATT_DISCOVER_SERVICES
      && msg_id == sl_bt_evt_gatt_service_id) {
    const sl_bt_evt_gatt_service_t *d = &evt->data.evt_gatt_service;
    if (d->connection != app_key_conn_handle) return;
    kgatt_service_handle = d->service;
    USER_LOG_INFO("[KGATT] service found handle=0x%08lx" USER_LOG_NL,
                  (unsigned long)kgatt_service_handle);
    return;
  }

  if (kgatt_state == KGATT_DISCOVER_CHARACTERISTICS
      && msg_id == sl_bt_evt_gatt_characteristic_id) {
    const sl_bt_evt_gatt_characteristic_t *d = &evt->data.evt_gatt_characteristic;
    if (d->connection != app_key_conn_handle) return;
    if (d->uuid.len == 16) {
      if (memcmp(d->uuid.data, UUID_CHAR_RX, 16) == 0) {
        kgatt_char_rx_handle = d->characteristic;
        USER_LOG_INFO("[KGATT] rx characteristic found (handle %u)" USER_LOG_NL,
                      kgatt_char_rx_handle);
      } else if (memcmp(d->uuid.data, UUID_CHAR_TX, 16) == 0) {
        kgatt_char_tx_handle = d->characteristic;
        USER_LOG_INFO("[KGATT] tx characteristic found (handle %u)" USER_LOG_NL,
                      kgatt_char_tx_handle);
      }
    }
    return;
  }

  // --- 过程完成（仅本模块的连接，且在发现流程中才处理） ---
  if (msg_id == sl_bt_evt_gatt_procedure_completed_id) {
    const sl_bt_evt_gatt_procedure_completed_t *d =
        &evt->data.evt_gatt_procedure_completed;
    if (d->connection != app_key_conn_handle) return;
    // 仅在本模块主动执行 GATT 发现时才处理，避免被 RAS 等其它 GATT 过程干扰
    if (kgatt_state == KGATT_IDLE || kgatt_state == KGATT_READY) return;

    if (d->result != SL_STATUS_OK) {
      USER_LOG_ERROR("[KGATT] procedure failed: 0x%04lx in state=%d" USER_LOG_NL,
                     (unsigned long)d->result, kgatt_state);
      kgatt_state = KGATT_IDLE;
      return;
    }

    switch (kgatt_state) {
      case KGATT_DISCOVER_SERVICES:
        if (kgatt_service_handle != 0) {
          kgatt_state = KGATT_DISCOVER_CHARACTERISTICS;
          USER_LOG_INFO("[KGATT] discovering characteristics" USER_LOG_NL);
          sl_bt_gatt_discover_characteristics(d->connection, kgatt_service_handle);
        } else {
          USER_LOG_ERROR("[KGATT] service not found" USER_LOG_NL);
          kgatt_state = KGATT_IDLE;
        }
        break;

      case KGATT_DISCOVER_CHARACTERISTICS:
        if (kgatt_char_tx_handle != 0) {
          kgatt_state = KGATT_ENABLE_CCCD;
          USER_LOG_INFO("[KGATT] enabling CCCD on tx (handle %u)" USER_LOG_NL,
                        kgatt_char_tx_handle);
          sl_bt_gatt_set_characteristic_notification(
              d->connection, kgatt_char_tx_handle, sl_bt_gatt_notification);
        } else {
          USER_LOG_ERROR("[KGATT] tx characteristic not found" USER_LOG_NL);
          kgatt_state = KGATT_IDLE;
        }
        break;

      case KGATT_ENABLE_CCCD:
        kgatt_state = KGATT_READY;
        USER_LOG_INFO("[KGATT] CCCD enabled, READY" USER_LOG_NL);
        if (kgatt_ready_callback != NULL) {
          kgatt_ready_callback(d->connection);
        }
        break;

      default:
        break;
    }
    return;
  }

  // --- 收到数据（仅本模块连接 + 本模块特征） ---
  if (msg_id == sl_bt_evt_gatt_characteristic_value_id) {
    const sl_bt_evt_gatt_characteristic_value_t *d =
        &evt->data.evt_gatt_characteristic_value;
    if (d->connection != app_key_conn_handle) return;
    if (d->characteristic == kgatt_char_tx_handle && kgatt_rx_callback != NULL) {
      kgatt_rx_callback(d->connection, d->value.data, (uint8_t)d->value.len);
    }
    return;
  }
}

/**
 * 主循环轮询。app_process_action() 每 tick 调用。
 * 安全网: 若状态机停在 IDLE 但连接已加密 → 启动 GATT 发现。
 * 用于兜底 sm_bonded 事件可能漏掉的重连场景。
 */
void key_gatt_comm_process_action(void)
{
  // 安全网：如果连接已存在且加密但还没开始发现
  if (kgatt_state != KGATT_IDLE) return;

  uint8_t conn = app_key_conn_handle;
  if (conn == SL_BT_INVALID_CONNECTION_HANDLE) return;

  uint8_t sec_mode = 0, key_size = 0, bond_h = 0;
  sl_status_t sc = sl_bt_connection_get_security_status(conn, &sec_mode, &key_size, &bond_h);
  if (sc == SL_STATUS_OK && sec_mode > (uint8_t)sl_bt_connection_mode1_level1) {
    USER_LOG_INFO("[KGATT] process_action: connection encrypted, starting discovery" USER_LOG_NL);
    kgatt_start_discovery();
  }
}

// 获取当前连接句柄 (供协议模块 getter 使用)
uint8_t key_gatt_comm_get_connection(void)
{
  return app_key_conn_handle;
}

bool key_gatt_comm_is_ready(void)
{
  return (kgatt_state == KGATT_READY);
}
