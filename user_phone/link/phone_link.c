/***************************************************************************//**
 * @file phone_link.c
 * @brief 手机链路面：广播/连接管理（不含 GATT 载荷处理）。
 *
 * V1.1 广播规范 (第 3 / 22.7 节):
 * - Advertising Data: Flags(06) + Manufacturer(B1 24 + advVersion + shortDid + deviceState + capabilityFlags)
 * - Scan Response: Complete Local Name "BLEKEY_XXXX"
 * - 广播间隔: Unbound=150ms, Bound=400ms, Silent=1000ms
 ******************************************************************************/

#include "user_phone/link/phone_link.h"

#include "sl_bt_api.h"
#include "em_device.h"
#include "sl_sleeptimer.h"
#include "user_log_console.h"
#include "user_phone/data/phone_adv.h"
#include "user_phone/data/phone_sm.h"
#include "user_phone/phone_cfg.h"

static uint8_t  phone_link_adv_handle  = 0xFFU;
static uint8_t  m_conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;

/* CR008-004: 当前物理链路的安全事实；已 Bond 不等于已被 APP 授权。 */
typedef struct {
  uint8_t bonding_handle;
  uint8_t security_mode;
} phone_link_security_state_t;

static phone_link_security_state_t phone_link_security_state;

/* 广播数据缓冲区 */
static uint8_t  phone_link_ad_buf[PHONE_ADV_DATA_MAX_LEN];
static uint8_t  phone_link_ad_len;
static uint8_t  phone_link_sr_buf[PHONE_SCAN_RESP_MAX_LEN];
static uint8_t  phone_link_sr_len;

// 广播刷新定时
static uint64_t phone_link_adv_refresh_deadline_ms;
// 广播恢复重试
static uint64_t phone_link_adv_recover_retry_at_ms;

// ---------------------------------------------------------------------------
// 当前连接安全状态
// ---------------------------------------------------------------------------
static void phone_link_security_reset(void)
{
  phone_link_security_state.bonding_handle = SL_BT_INVALID_BONDING_HANDLE;
  phone_link_security_state.security_mode = sl_bt_connection_mode1_level1;
}

static const char *phone_link_security_mode_name(uint8_t security_mode)
{
  switch (security_mode) {
    case sl_bt_connection_mode1_level1: return "L1";
    case sl_bt_connection_mode1_level2: return "L2";
    case sl_bt_connection_mode1_level3: return "L3";
    case sl_bt_connection_mode1_level4: return "L4";
    default:                            return "UNKNOWN";
  }
}

static void phone_link_security_log(const char *event)
{
  USER_LOG_INFO("[PHONE] LINK_SECURITY %s conn=%u bond=0x%02X mode=%s(0x%02X) bonded=%s encrypted=%s"
                USER_LOG_NL,
                event,
                (unsigned)m_conn_handle,
                (unsigned)phone_link_security_state.bonding_handle,
                phone_link_security_mode_name(phone_link_security_state.security_mode),
                (unsigned)phone_link_security_state.security_mode,
                phone_link_current_is_bonded() ? "Y" : "N",
                phone_link_current_is_encrypted() ? "Y" : "N");
}

// ---------------------------------------------------------------------------
// 单调毫秒时间
// ---------------------------------------------------------------------------
static uint64_t phone_link_now_ms(void)
{
  uint64_t freq = sl_sleeptimer_get_timer_frequency();
  uint64_t tick = sl_sleeptimer_get_tick_count64();
  return (tick * 1000ULL) / freq;
}

static void phone_link_adv_refresh_arm(void)
{
  phone_link_adv_refresh_deadline_ms =
    phone_link_now_ms() + (uint64_t)PHONE_LINK_ADV_DEFAULT_REFRESH_MS;
}

static void phone_link_adv_recover_request(uint32_t delay_ms)
{
  phone_link_adv_recover_retry_at_ms = phone_link_now_ms() + (uint64_t)delay_ms;
}

// ---------------------------------------------------------------------------
// 构建并写入广播数据 (Advertising Data + Scan Response)
// ---------------------------------------------------------------------------
static sl_status_t phone_link_adv_set_data(void)
{
  sl_status_t sc;
  uint8_t device_state = phone_sm_get_device_state();

  phone_adv_build_advertising_data(device_state, phone_link_ad_buf, &phone_link_ad_len);
  sc = sl_bt_legacy_advertiser_set_data(phone_link_adv_handle,
                                        (uint8_t)sl_bt_advertiser_advertising_data_packet,
                                        phone_link_ad_len,
                                        phone_link_ad_buf);
  if (sc != SL_STATUS_OK) {
    return sc;
  }

  phone_adv_build_scan_response(phone_link_sr_buf, &phone_link_sr_len);
  sc = sl_bt_legacy_advertiser_set_data(phone_link_adv_handle,
                                        (uint8_t)sl_bt_advertiser_scan_response_packet,
                                        phone_link_sr_len,
                                        phone_link_sr_buf);
  return sc;
}

// ---------------------------------------------------------------------------
// 周期性刷新广播数据（更新 shortDid / deviceState 等）
// ---------------------------------------------------------------------------
static void phone_link_adv_refresh_process(void)
{
  if (phone_link_adv_handle == 0xFFU) return;
  if (phone_sm_is_connected())           return;
  if (phone_link_adv_refresh_deadline_ms == 0U) return;
  if (phone_link_now_ms() < phone_link_adv_refresh_deadline_ms) return;

  (void)phone_link_adv_set_data();
  phone_link_adv_refresh_arm();
}

// ---------------------------------------------------------------------------
// 广播恢复（断开连接后自动重新开始广播）
// ---------------------------------------------------------------------------
static void phone_link_adv_recover_process(void)
{
  sl_status_t adv_sc, sc;

  if (phone_link_adv_recover_retry_at_ms == 0U) return;
  if (phone_link_now_ms() < phone_link_adv_recover_retry_at_ms) return;
  if (phone_sm_is_connected() || phone_link_adv_handle == 0xFFU) {
    phone_link_adv_recover_retry_at_ms = 0U;
    return;
  }

  adv_sc = phone_link_adv_set_data();
  if (adv_sc != SL_STATUS_OK) {
    uint32_t delay = (adv_sc == (sl_status_t)SL_STATUS_ISR) ? 20U : 200U;
    phone_link_adv_recover_request(delay);
    return;
  }

  sc = sl_bt_legacy_advertiser_start(phone_link_adv_handle,
                                     sl_bt_legacy_advertiser_connectable);
  if (sc != SL_STATUS_OK) {
    uint32_t delay = (sc == (sl_status_t)SL_STATUS_ISR) ? 20U : 200U;
    phone_link_adv_recover_request(delay);
    return;
  }

  phone_link_adv_recover_retry_at_ms = 0U;
  phone_link_adv_refresh_arm();
  USER_LOG_INFO("[PHONE] Advertising recovered" USER_LOG_NL);
}

// ---------------------------------------------------------------------------
// 启动可连接广播
// ---------------------------------------------------------------------------
static void phone_link_start_adv(void)
{
  sl_status_t sc;
  uint8_t device_state = phone_sm_get_device_state();
  uint16_t interval = phone_adv_get_interval(device_state);

  sc = phone_link_adv_set_data();
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[PHONE] adv set data failed 0x%04lX" USER_LOG_NL, (unsigned long)sc);
    return;
  }

  sc = sl_bt_advertiser_set_timing(phone_link_adv_handle,
                                   interval, interval, 0, 0);
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[PHONE] advertiser_set_timing failed 0x%04lX" USER_LOG_NL, (unsigned long)sc);
    return;
  }

  sc = sl_bt_legacy_advertiser_start(phone_link_adv_handle,
                                     sl_bt_legacy_advertiser_connectable);
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[PHONE] advertiser_start failed 0x%04lX" USER_LOG_NL, (unsigned long)sc);
    return;
  }

  USER_LOG_INFO("[PHONE] Advertising started (connectable, interval=%ums, state=0x%02X)" USER_LOG_NL,
                (unsigned int)((uint32_t)interval * 5U / 8U), (unsigned int)device_state);
  phone_link_adv_refresh_arm();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

static void phone_link_set_public_identity(void)
{
  /* V1.2: 显式读取出厂公开 MAC (EUI48) 并设为 identity 地址,
   * 保证 HID 绑定/Bond 基于稳定的出厂地址跨上电保持 */
  uint32_t eui48l = DEVINFO->EUI48L;
  uint32_t eui48h = DEVINFO->EUI48H;

  /* 未编程 (全 FF) 时跳过, 保留 SDK 默认 identity */
  if (eui48l == 0xFFFFFFFFUL && (eui48h & 0xFFFFU) == 0xFFFFU) {
    USER_LOG_WARN("[PHONE] EUI48 unprogrammed, keep default identity" USER_LOG_NL);
    return;
  }

  bd_addr addr;
  addr.addr[0] = (uint8_t)(eui48l & 0xFFU);
  addr.addr[1] = (uint8_t)((eui48l >> 8) & 0xFFU);
  addr.addr[2] = (uint8_t)((eui48l >> 16) & 0xFFU);
  addr.addr[3] = (uint8_t)((eui48l >> 24) & 0xFFU);
  addr.addr[4] = (uint8_t)(eui48h & 0xFFU);
  addr.addr[5] = (uint8_t)((eui48h >> 8) & 0xFFU);

  sl_status_t sc = sl_bt_gap_set_identity_address(addr, sl_bt_gap_public_address);
  USER_LOG_INFO("[PHONE] identity := public %02X:%02X:%02X:%02X:%02X:%02X sc=0x%04lx" USER_LOG_NL,
                (unsigned)addr.addr[5], (unsigned)addr.addr[4], (unsigned)addr.addr[3],
                (unsigned)addr.addr[2], (unsigned)addr.addr[1], (unsigned)addr.addr[0],
                (unsigned long)sc);
}

void phone_link_init(void)
{
  phone_link_adv_handle  = 0xFFU;
  m_conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
  phone_link_security_reset();
  phone_link_adv_refresh_deadline_ms  = 0U;
  phone_link_adv_recover_retry_at_ms  = 0U;

  /* V1.2: 出厂公开 MAC 作为 identity 地址 (HID 绑定/系统回连基于稳定地址) */
  phone_link_set_public_identity();

  /* V1.2: SM 安全配置 (SC-only + BONDING_REQUIRED + NoInputNoOutput, 默认 Non-Bondable, max_bonds=2)
   *        注: 默认 NoIO 不能加 MITM_REQUIRED (会 INVALID_PARAMETER);
   *        MITM 只在 PASSIVE_PAIR_READY 临时切 DisplayOnly 时开启 */
  {
    sl_status_t sc;
    sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_SC_ONLY
                            | SL_BT_SM_CONFIGURATION_BONDING_REQUIRED
                            | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED,
                             sl_bt_sm_io_capability_noinputnooutput);
    USER_LOG_INFO("[PHONE] init: sm_configure(SC+BR+BondConfirm+NoIO) sc=0x%04lx" USER_LOG_NL, (unsigned long)sc);
    sc = sl_bt_sm_store_bonding_configuration(2, 0);
    USER_LOG_INFO("[PHONE] init: sm_store_bonds(2) sc=0x%04lx" USER_LOG_NL, (unsigned long)sc);
    sc = sl_bt_sm_set_bondable_mode(0);
    USER_LOG_INFO("[PHONE] init: sm_set_bondable(0) sc=0x%04lx" USER_LOG_NL, (unsigned long)sc);
  }
}

void phone_link_process_action(void)
{
  phone_link_adv_recover_process();
  phone_link_adv_refresh_process();
}

uint8_t phone_link_conn_handle(void)
{
  return m_conn_handle;
}

bool phone_link_current_is_bonded(void)
{
  return m_conn_handle != SL_BT_INVALID_CONNECTION_HANDLE
         && phone_link_security_state.bonding_handle
              != SL_BT_INVALID_BONDING_HANDLE;
}

bool phone_link_current_is_encrypted(void)
{
  uint8_t mode = phone_link_security_state.security_mode;

  return m_conn_handle != SL_BT_INVALID_CONNECTION_HANDLE
         && (mode == sl_bt_connection_mode1_level2
             || mode == sl_bt_connection_mode1_level3
             || mode == sl_bt_connection_mode1_level4);
}

uint8_t phone_link_current_bonding_handle_get(void)
{
  return phone_link_security_state.bonding_handle;
}

uint8_t phone_link_current_security_mode_get(void)
{
  return phone_link_security_state.security_mode;
}

void phone_link_switch_to_secondary(void)
{
  if (phone_link_adv_handle == 0xFFU) return;

  sl_status_t sc = phone_link_adv_set_data();
  if (sc == SL_STATUS_OK) {
    USER_LOG_INFO("[PHONE] Switched to secondary adv (state=0x%02X)" USER_LOG_NL,
                  (unsigned int)phone_sm_get_device_state());
  }
}

void phone_link_switch_to_primary(void)
{
  if (phone_link_adv_handle == 0xFFU) return;

  sl_status_t sc = phone_link_adv_set_data();
  if (sc == SL_STATUS_OK) {
    USER_LOG_INFO("[PHONE] Switched to primary adv (state=0x%02X)" USER_LOG_NL,
                  (unsigned int)phone_sm_get_device_state());
    phone_link_adv_refresh_arm();
  }
}

void phone_link_on_bt_event(sl_bt_msg_t *evt)
{
  if (evt == NULL) return;

  switch (SL_BT_MSG_ID(evt->header)) {

    case sl_bt_evt_system_boot_id: {
      /* 仅诊断：读取 Bluetooth Core（工具配置）当前生效的全局 TX 功率，
       * 不修改任何功率设置。单位均为 0.1 dBm / 0.1 dB。 */
      {
        int16_t support_min;
        int16_t support_max;
        int16_t set_min;
        int16_t set_max;
        int16_t rf_path_gain;
        sl_status_t power_sc = sl_bt_system_get_tx_power_setting(&support_min,
                                                                   &support_max,
                                                                   &set_min,
                                                                   &set_max,
                                                                   &rf_path_gain);
        if (power_sc == SL_STATUS_OK) {
          USER_LOG_INFO("[PHONE] TX power: support=[%d,%d], set=[%d,%d] (0.1dBm), rf_path_gain=%d (0.1dB)" USER_LOG_NL,
                        (int)support_min, (int)support_max,
                        (int)set_min, (int)set_max,
                        (int)rf_path_gain);
        } else {
          USER_LOG_ERROR("[PHONE] get_tx_power_setting failed 0x%04lX" USER_LOG_NL,
                         (unsigned long)power_sc);
        }
      }

      sl_status_t sc = sl_bt_advertiser_create_set(&phone_link_adv_handle);
      if (sc != SL_STATUS_OK) {
        USER_LOG_ERROR("[PHONE] advertiser_create_set failed 0x%04lX" USER_LOG_NL, (unsigned long)sc);
        phone_link_adv_handle = 0xFFU;
        break;
      }
      phone_link_start_adv();
      break;
    }

    case sl_bt_evt_connection_opened_id: {
      const sl_bt_evt_connection_opened_t *d = &evt->data.evt_connection_opened;
      if (d->role != sl_bt_connection_role_peripheral) break;

      m_conn_handle = d->connection;
      phone_link_security_state.bonding_handle = d->bonding;
      phone_link_security_state.security_mode = sl_bt_connection_mode1_level1;
      phone_link_security_log("OPEN");
      phone_sm_on_connection_opened(d->connection, d->bonding);

      if (phone_link_adv_handle != 0xFFU) {
        sl_status_t sc = sl_bt_advertiser_stop(phone_link_adv_handle);
        if (sc == SL_STATUS_OK) {
          USER_LOG_INFO("[PHONE] Advertising stopped (connected)" USER_LOG_NL);
        }
      }
      phone_link_adv_refresh_deadline_ms = 0U;
      break;
    }

    case sl_bt_evt_connection_parameters_id: {
      const sl_bt_evt_connection_parameters_t *d =
          &evt->data.evt_connection_parameters;
      if (d->connection != m_conn_handle) break;

      if (phone_link_security_state.security_mode != d->security_mode) {
        phone_link_security_state.security_mode = d->security_mode;
        phone_link_security_log("SECURITY_CHANGED");
      }
      break;
    }

    case sl_bt_evt_sm_bonded_id: {
      const sl_bt_evt_sm_bonded_t *d = &evt->data.evt_sm_bonded;
      if (d->connection != m_conn_handle) break;

      phone_link_security_state.bonding_handle = d->bonding;
      phone_link_security_state.security_mode = d->security_mode;
      phone_link_security_log("BONDED");
      break;
    }

    case sl_bt_evt_connection_closed_id: {
      const sl_bt_evt_connection_closed_t *d = &evt->data.evt_connection_closed;
      if (d->connection != m_conn_handle) break;

      USER_LOG_INFO("[PHONE] Disconnected conn=%u reason=0x%04X" USER_LOG_NL,
                    d->connection, d->reason);
      phone_sm_on_connection_closed(
          d->connection,
          phone_link_security_state.bonding_handle,
          phone_link_security_state.security_mode);
      phone_link_security_reset();
      phone_link_security_log("CLOSED");
      m_conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;

      if (!phone_sm_is_connected() && phone_link_adv_handle != 0xFFU) {
        phone_link_adv_recover_request(0U);
        USER_LOG_INFO("[PHONE] Adv restore scheduled after disconnect" USER_LOG_NL);
      }
      break;
    }

    default:
      break;
  }
}
