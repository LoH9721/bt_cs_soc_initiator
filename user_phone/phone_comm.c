/***************************************************************************//**
 * @file phone_comm.c
 * @brief 手机通信门面：委托 link 层处理连接/广播, 委托 sm 层处理协议。
 *
 * V1.1 重写: 旧 phone_auth/phone_cmd 已废弃, 全部业务逻辑由 phone_sm 接管。
 ******************************************************************************/

#include "user_phone/phone_comm.h"
#include "user_phone/phone_cfg.h"

#include "gatt_db.h"
#include "user_log_console.h"
#include "user_phone/link/phone_link.h"
#include "user_phone/data/phone_sm.h"
#include "user_phone/data/phone_rang.h"

/* ========================================================================== */
/* 内部: 发送回调 (phone_sm → Notify)                                        */
/* ========================================================================== */

static sl_status_t phone_comm_notify_send(const uint8_t *data, uint16_t len)
{
  /* 委托 phone_link 或直接使用 sl_bt_gatt_server_send_notification */
  /* phone_link 已处理连接句柄, 这里直接调用 GATT 发送 */
  uint8_t conn = phone_link_conn_handle();
  if (conn == 0xFFU) return SL_STATUS_INVALID_STATE;
  if (data == NULL && len > 0U) return SL_STATUS_INVALID_PARAMETER;

  return sl_bt_gatt_server_send_notification(conn, gattdb_phone_tx,
                                             (size_t)len, data);
}

static void phone_comm_disconnect(void)
{
  uint8_t conn = phone_link_conn_handle();
  if (conn != 0xFFU) {
    (void)sl_bt_connection_close(conn);
  }
}

/* ========================================================================== */
/* Public API                                                                  */
/* ========================================================================== */

void phone_comm_init(void)
{
  phone_link_init();
  phone_rang_init();
  phone_sm_init(phone_comm_notify_send, phone_comm_disconnect);
}

void phone_comm_process_action(void)
{
  phone_link_process_action();
  phone_sm_process_action();
  phone_rang_process_action();
}

void phone_comm_on_bt_event(sl_bt_msg_t *evt)
{
  if (evt == NULL) return;

  /* CR008-008: 协议栈启动完成后先校验 Passive/授权 Bond，再由 link
   * 构造首个广播，避免仅凭 passive_enabled 盲目加入 HID 特征。 */
  if (SL_BT_MSG_ID(evt->header) == sl_bt_evt_system_boot_id) {
    phone_sm_on_system_boot();
  }

  /* 其余事件仍由 link 先更新连接/安全状态，再通知 SM。 */
  phone_link_on_bt_event(evt);

  switch (SL_BT_MSG_ID(evt->header)) {

    case sl_bt_evt_connection_opened_id:
      /* CR008-003: 新连接必须从空测距状态开始，禁止复用上一连接结果。 */
      phone_rang_reset();
      break;

    case sl_bt_evt_gatt_server_attribute_value_id: {
      const sl_bt_evt_gatt_server_attribute_value_t *d =
          &evt->data.evt_gatt_server_attribute_value;
      if (d->attribute == (uint16_t)gattdb_phone_rx) {
#ifdef PHONE_DUMP_FRAMES
        /* GATT 原始数据 dump: 使用 static 避免栈溢出 (768 字节 → 0) */
        {
          static char raw_hex[768];  /* static, 不在栈上 */
          uint16_t i, hex_len = (d->value.len < 255U) ? d->value.len : 255U;
          uint16_t pos = 0U;
          for (i = 0U; i < hex_len && pos < sizeof(raw_hex) - 5U; i++) {
            pos += (uint16_t)snprintf(&raw_hex[pos], sizeof(raw_hex) - pos,
                                      "%02X ", (unsigned)d->value.data[i]);
          }
          if (d->value.len > 255U) {
            pos += (uint16_t)snprintf(&raw_hex[pos], sizeof(raw_hex) - pos,
                                      "...(%u total)", (unsigned)d->value.len);
          }
          USER_LOG_INFO("[GATT] RX_RAW len=%u: %s" USER_LOG_NL,
                        (unsigned)d->value.len, raw_hex);
        }
#endif
        phone_sm_on_receive(d->value.data, d->value.len);
      }
      break;
    }

    case sl_bt_evt_gatt_server_characteristic_status_id: {
      const sl_bt_evt_gatt_server_characteristic_status_t *d =
          &evt->data.evt_gatt_server_characteristic_status;
      if (d->characteristic == (uint16_t)gattdb_phone_tx
          && d->status_flags == (uint8_t)sl_bt_gatt_server_client_config) {
        bool enabled = (d->client_config_flags
                        == (uint16_t)sl_bt_gatt_server_notification
                        || d->client_config_flags
                        == (uint16_t)sl_bt_gatt_server_notification_and_indication);
        USER_LOG_INFO("[PHONE] TX CCCD %s (flags=0x%04X)" USER_LOG_NL,
                      enabled ? "ENABLED" : "DISABLED",
                      (unsigned)d->client_config_flags);
        if (enabled) {
          phone_sm_on_notify_enabled();
        }
      }
      break;
    }

    case sl_bt_evt_gatt_mtu_exchanged_id:
      phone_sm_on_mtu_exchanged(evt->data.evt_gatt_mtu_exchanged.mtu);
      break;

    /* V1.2: BLE SM Pairing 事件 */
    case sl_bt_evt_sm_confirm_bonding_id:
      phone_sm_on_sm_confirm_bonding(
          evt->data.evt_sm_confirm_bonding.connection,
          evt->data.evt_sm_confirm_bonding.bonding_handle);
      break;

    case sl_bt_evt_sm_bonded_id:
      phone_sm_on_sm_bonded(evt->data.evt_sm_bonded.connection,
                            evt->data.evt_sm_bonded.bonding,
                            evt->data.evt_sm_bonded.security_mode);
      break;

    case sl_bt_evt_sm_bonding_failed_id:
      phone_sm_on_sm_bonding_failed(evt->data.evt_sm_bonding_failed.connection,
                                    evt->data.evt_sm_bonding_failed.reason);
      break;

    default:
      break;
  }
}

/* ========================================================================== */
/* 已有接口 (保持兼容)                                                        */
/* ========================================================================== */

bool phone_comm_is_connected(void)
{
  return phone_sm_is_connected();
}

sl_status_t phone_comm_send(const uint8_t *data, uint16_t len)
{
  return phone_comm_notify_send(data, len);
}

uint8_t phone_comm_connection_handle_get(void)
{
  return phone_link_conn_handle();
}

bool phone_comm_current_link_is_bonded(void)
{
  return phone_link_current_is_bonded();
}

bool phone_comm_current_link_is_encrypted(void)
{
  return phone_link_current_is_encrypted();
}

uint8_t phone_comm_current_bonding_handle_get(void)
{
  return phone_link_current_bonding_handle_get();
}

uint8_t phone_comm_current_security_mode_get(void)
{
  return phone_link_current_security_mode_get();
}

bool phone_comm_current_link_is_authorized_passive(void)
{
  uint8_t bonding = phone_link_current_bonding_handle_get();

  return phone_sm_is_passive_enabled()
         && phone_sm_is_authorized_bonding(bonding)
         && phone_link_current_security_mode_get()
              == sl_bt_connection_mode1_level4;
}

bool phone_comm_consume_authorized_passive_disconnect(void)
{
  return phone_sm_consume_authorized_passive_disconnect();
}

bool phone_comm_normal_actions_allowed(void)
{
  return phone_sm_is_authenticated();
}

void phone_comm_switch_to_secondary(void)
{
  phone_link_switch_to_secondary();
}

void phone_comm_switch_to_primary(void)
{
  phone_link_switch_to_primary();
}

void phone_comm_disconnect_for_factory_reset(void)
{
  phone_comm_disconnect();
}

/* ========================================================================== */
/* V1.1 新增                                                                   */
/* ========================================================================== */

uint8_t phone_comm_get_pending_control_cmd(void)
{
  return phone_sm_get_pending_control_cmd();
}

uint8_t phone_comm_get_vehicle_lock_state(void)
{
  return phone_sm_get_vehicle_lock_state();
}

void phone_comm_notify_vehicle_lock_state(uint8_t lock_state)
{
  phone_sm_notify_vehicle_lock_state(lock_state);
}

/* V1.2 新增: 完整车辆状态快照通知 */
void phone_comm_notify_vehicle_state(uint8_t lock, uint8_t ignition,
                                     uint16_t range, uint8_t doors)
{
  phone_sm_notify_vehicle_state(lock, ignition, range, doors);
}

void phone_comm_set_auth_condition(bool met)
{
  phone_sm_set_auth_condition(met);
}

bool phone_comm_is_authenticated(void)
{
  return phone_sm_is_authenticated();
}

bool phone_comm_is_notify_enabled(void)
{
  return phone_sm_is_notify_enabled();
}

bool phone_comm_is_passive_enabled(void)
{
  return phone_sm_is_passive_enabled();
}

bool phone_comm_is_silent(void)
{
  return phone_sm_is_silent();
}

bool phone_comm_consume_passive_quota(void)
{
  return phone_sm_consume_passive_quota();
}

uint32_t phone_comm_get_passive_quota(void)
{
  return phone_sm_get_passive_quota();
}

uint8_t phone_comm_get_passive_sensitivity(void)
{
  return phone_sm_get_passive_sensitivity();
}
