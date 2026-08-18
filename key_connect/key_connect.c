#include "key_connect.h"
#include "app.h"
#include "user_log_console.h"
#include "ble_peer_manager_central.h"
#include "ble_peer_manager_filter.h"
#include "sl_status.h"
#include "sl_bluetooth.h"
#include "sl_component_catalog.h"
#include "sl_string.h"
#include "sl_iostream.h"
#include "sl_sleeptimer.h"

// -----------------------------------------------------------------------------
// 状态机
// -----------------------------------------------------------------------------
typedef enum {
  KEY_CONN_IDLE,
  KEY_CONN_NORMAL_SCAN,
  KEY_CONN_PAIRING_SCAN,
  KEY_CONN_CONNECTED,
} key_connect_state_t;

static key_connect_state_t kc_state = KEY_CONN_IDLE;

// -----------------------------------------------------------------------------
// 定时变量 (ms)
// -----------------------------------------------------------------------------
#define KC_SCAN_KEEPALIVE_MS   3000
#define KC_PAIRING_WINDOW_MS   60000
#define KC_PAIRING_REMIND_MS   10000
#define KC_SMP_DELAY_MS        15
#define KC_SMP_FALLBACK_MS     600

static uint64_t kc_scan_keepalive_next_ms;
static uint64_t kc_pairing_deadline_ms;
static uint64_t kc_pairing_remind_next_ms;
static uint64_t kc_smp_increase_at_ms;
static uint64_t kc_smp_fallback_at_ms;
static bool     kc_smp_increase_done;
static bool     kc_smp_from_pairing;
static uint8_t  kc_connected_conn;
static bool     kc_sm_bonded = false;

// 诊断计数器
static uint32_t kc_scan_restart_cnt;
static uint32_t kc_normal_rpt_cnt;
static uint64_t kc_diag_next_ms;
#define KC_DIAG_INTERVAL_MS 10000

// -----------------------------------------------------------------------------
// 辅助: 获取当前 ms 时间
// -----------------------------------------------------------------------------
static uint64_t kc_now_ms(void)
{
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count64());
}

// -----------------------------------------------------------------------------
// 辅助: 扫描重启
// -----------------------------------------------------------------------------
static void kc_scan_restart(void)
{
  kc_scan_restart_cnt++;

#if defined(SL_CATALOG_BLUETOOTH_FEATURE_ACCEPT_LIST_PRESENT)
  // 硬件 Accept List 过滤策略: NORMAL_SCAN 仅上报白名单设备, PAIRING_SCAN 上报全部
  {
    uint8_t filter_policy = (kc_state == KEY_CONN_NORMAL_SCAN)
        ? sl_bt_scanner_filter_policy_basic_filtered
        : sl_bt_scanner_filter_policy_basic_unfiltered;
    sl_bt_scanner_set_parameters_and_filter(
        sl_bt_scanner_scan_mode_passive, 16, 16, 0, filter_policy);
  }
#endif

  sl_status_t sc = ble_peer_manager_central_create_connection();
  if (sc == SL_STATUS_OK) {
    USER_LOG_INFO("[KEY_CONN] scan restarted (#%lu)" USER_LOG_NL,
                  (unsigned long)kc_scan_restart_cnt);
  } else {
    USER_LOG_INFO("[KEY_CONN] scan restart busy 0x%04lx (#%lu)" USER_LOG_NL,
                  (unsigned long)sc, (unsigned long)kc_scan_restart_cnt);
  }
}

// -----------------------------------------------------------------------------
// 加载 NVM 中已存储的绑定设备到软件白名单 + 硬件 Accept List
// bonding handles 以位掩码返回: bondings[N] 的 bit M 代表 handle (N*8+M)
// -----------------------------------------------------------------------------
static void kc_dump_bonding_table(void)
{
  uint32_t num_bondings = 0;
  size_t bondings_len = 0;
  uint8_t bondings[8] = {0};
  sl_status_t sc = sl_bt_sm_get_bonding_handles(0, &num_bondings,
                                                 sizeof(bondings), &bondings_len, bondings);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[KEY_CONN] bonding list: get_handles err 0x%04lx" USER_LOG_NL,
                  (unsigned long)sc);
    return;
  }
  USER_LOG_INFO("[KEY_CONN] bonding list: %lu total, mask_bytes=%u" USER_LOG_NL,
                (unsigned long)num_bondings, (unsigned)bondings_len);
  if (num_bondings == 0) {
    USER_LOG_INFO("[KEY_CONN] WARNING: No bonded devices in NVM!" USER_LOG_NL);
    return;
  }

#if defined(SL_CATALOG_BLUETOOTH_FEATURE_ACCEPT_LIST_PRESENT)
  sl_bt_accept_list_remove_all_devices();
#endif

  // 按位掩码遍历 bonding handles
  for (size_t i = 0; i < bondings_len && i < sizeof(bondings); i++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      if (!(bondings[i] & (1u << bit))) continue;
      uint32_t h = (uint32_t)(i * 8 + bit);
      bd_addr addr;
      uint8_t addr_type = 0, sec_mode = 0, key_size = 0;
      sc = sl_bt_sm_get_bonding_details(h, &addr, &addr_type, &sec_mode, &key_size);
      if (sc != SL_STATUS_OK) continue;

      USER_LOG_INFO("[KEY_CONN]   bond[%lu] %02X:%02X:%02X:%02X:%02X:%02X type=%u sec=%u" USER_LOG_NL,
                    (unsigned long)h,
                    addr.addr[5], addr.addr[4], addr.addr[3],
                    addr.addr[2], addr.addr[1], addr.addr[0],
                    addr_type, sec_mode);

      // 软件白名单 (peer manager filter)
      sl_status_t wl_sc = ble_peer_manager_add_allowed_bt_address(&addr);
      if (wl_sc == SL_STATUS_OK) {
        USER_LOG_INFO("[KEY_CONN]   -> added to sw whitelist" USER_LOG_NL);
      } else if (wl_sc != SL_STATUS_ALREADY_EXISTS) {
        USER_LOG_INFO("[KEY_CONN]   -> sw wl err 0x%04lx" USER_LOG_NL, (unsigned long)wl_sc);
      }

      // 硬件 Accept List (controller 级过滤)
#if defined(SL_CATALOG_BLUETOOTH_FEATURE_ACCEPT_LIST_PRESENT)
      sl_status_t al_sc = sl_bt_accept_list_add_device_by_bonding(h);
      if (al_sc == SL_STATUS_OK) {
        USER_LOG_INFO("[KEY_CONN]   -> added to hw accept list" USER_LOG_NL);
      } else {
        USER_LOG_INFO("[KEY_CONN]   -> hw accept list err 0x%04lx" USER_LOG_NL, (unsigned long)al_sc);
      }
#endif
    }
  }
}

// -----------------------------------------------------------------------------
// CRC16-CCITT-FALSE
// -----------------------------------------------------------------------------
static uint16_t key_connect_crc16(const uint8_t *data, uint8_t len)
{
  uint16_t crc = 0xFFFF;
  for (uint8_t i = 0; i < len; i++) {
    crc ^= (uint16_t)data[i] << 8;
    for (uint8_t j = 0; j < 8; j++) {
      if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
      else crc <<= 1;
    }
  }
  return crc;
}

// -----------------------------------------------------------------------------
// 解析 Manufacturer Specific Data — 返回 ID 字节
// -----------------------------------------------------------------------------
static bool key_connect_parse_adv_data(const uint8_t *adv_data, uint8_t adv_len,
                                       uint8_t *out_id)
{
  uint8_t offset = 0;
  while (offset + 2 <= adv_len) {
    uint8_t ad_len = adv_data[offset];
    if (ad_len == 0) break;
    if (offset + ad_len + 1 > adv_len) break;
    if (adv_data[offset + 1] == 0xFF && ad_len >= 14) {
      const uint8_t *mfr = &adv_data[offset + 2];
      if (mfr[0] == 0xFF && mfr[1] == 0x02) {
        const uint8_t *p = mfr + 2;
        uint16_t crc_calc = key_connect_crc16(p, 9);
        uint16_t crc_recv = (uint16_t)p[9] | ((uint16_t)p[10] << 8);
        if (crc_calc == crc_recv) { *out_id = p[8]; return true; }
      }
    }
    offset += ad_len + 1;
  }
  return false;
}

// -----------------------------------------------------------------------------
// 进入 NORMAL_SCAN
// -----------------------------------------------------------------------------
static void kc_enter_normal_scan(void)
{
  kc_state = KEY_CONN_NORMAL_SCAN;
  kc_pairing_deadline_ms = 0;
  kc_scan_keepalive_next_ms = kc_now_ms() + KC_SCAN_KEEPALIVE_MS;
  kc_diag_next_ms = kc_now_ms() + KC_DIAG_INTERVAL_MS;
  kc_normal_rpt_cnt = 0;

  // 正常模式: 使用 peer manager 默认过滤 (地址白名单如有配置)
  kc_scan_restart();

  // 诊断: 检查 filter 是否已配置
  if (!ble_peer_manager_is_filter_set()) {
    USER_LOG_INFO("[KEY_CONN] WARNING: No filter configured, scan reports will be IGNORED!" USER_LOG_NL);
    USER_LOG_INFO("[KEY_CONN] Hint: use 'auto_conn' + 'wl_add <addr>' to configure whitelist" USER_LOG_NL);
  }
}

// -----------------------------------------------------------------------------
// Public API
// -----------------------------------------------------------------------------
// 这些函数实现了连接管理的核心逻辑，包括状态机处理、扫描控制、连接建立和断开、配对模式等。主循环通过调用 key_connect_process_action 来维护状态机和定时器，BLE事件通过 key_connect_on_bt_event 处理，Peer Manager事件通过 key_connect_on_peer_manager_event 处理。
void key_connect_process_action(void)
{
  uint64_t now = kc_now_ms();

  // --- 扫描 keepalive ---
  if ((kc_state == KEY_CONN_NORMAL_SCAN || kc_state == KEY_CONN_PAIRING_SCAN)
      && now >= kc_scan_keepalive_next_ms) {
    kc_scan_keepalive_next_ms = now + KC_SCAN_KEEPALIVE_MS;
    if (app_key_conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
      kc_scan_restart();
    }
  }

  // --- 周期诊断打印 (每 KC_DIAG_INTERVAL_MS) ---
  if ((kc_state == KEY_CONN_NORMAL_SCAN || kc_state == KEY_CONN_PAIRING_SCAN)
      && kc_diag_next_ms != 0 && now >= kc_diag_next_ms) {
    kc_diag_next_ms = now + KC_DIAG_INTERVAL_MS;
    USER_LOG_INFO("[KEY_CONN] diag: state=%d scan_restarts=%lu adv_reports=%lu filter=%s" USER_LOG_NL,
                  kc_state,
                  (unsigned long)kc_scan_restart_cnt,
                  (unsigned long)kc_normal_rpt_cnt,
                  ble_peer_manager_is_filter_set() ? "yes" : "NO");
    kc_normal_rpt_cnt = 0;
  }

  // --- 配对超时 ---
  if (kc_state == KEY_CONN_PAIRING_SCAN && kc_pairing_deadline_ms != 0
      && now >= kc_pairing_deadline_ms) {
    USER_LOG_INFO("[KEY_CONN] pairing scan window expired, back to NORMAL_SCAN" USER_LOG_NL);
    kc_enter_normal_scan();
  }

  // --- 配对提醒打印 ---
  if (kc_state == KEY_CONN_PAIRING_SCAN && kc_pairing_remind_next_ms != 0
      && now >= kc_pairing_remind_next_ms) {
    uint64_t remain = (kc_pairing_deadline_ms > now) ? (kc_pairing_deadline_ms - now) : 0;
    USER_LOG_INFO("[KEY_CONN] pairing scan: %llu s remaining" USER_LOG_NL,
                  (unsigned long long)(remain / 1000));
    kc_pairing_remind_next_ms = now + KC_PAIRING_REMIND_MS;
  }

  // --- 延迟 SMP increase_security ---
  if (kc_state == KEY_CONN_CONNECTED && !kc_smp_increase_done
      && kc_connected_conn != SL_BT_INVALID_CONNECTION_HANDLE) {
    bool fire = false;
    if (kc_smp_increase_at_ms != 0 && now >= kc_smp_increase_at_ms) fire = true;
    if (kc_smp_fallback_at_ms != 0 && now >= kc_smp_fallback_at_ms) fire = true;
    if (fire) {
      kc_smp_increase_at_ms = 0;
      kc_smp_fallback_at_ms = 0;
      // 检查当前安全状态
      uint8_t sec_mode = 0, key_size = 0, bond_h = 0;
      sl_status_t sc = sl_bt_connection_get_security_status(kc_connected_conn,
                                                            &sec_mode, &key_size, &bond_h);
      if (sc == SL_STATUS_OK && sec_mode > (uint8_t)sl_bt_connection_mode1_level1) {
        USER_LOG_INFO("[KEY_CONN] already encrypted (mode=%u), sm_increase_security skipped" USER_LOG_NL,
                      sec_mode);
        kc_smp_increase_done = true;
        return;
      }
      USER_LOG_INFO("[KEY_CONN] sm_increase_security (conn=%u)" USER_LOG_NL, kc_connected_conn);
      sc = sl_bt_sm_increase_security(kc_connected_conn);
      if (sc == SL_STATUS_OK) {
        USER_LOG_INFO("[KEY_CONN] sm_increase_security OK" USER_LOG_NL);
      } else {
        USER_LOG_INFO("[KEY_CONN] sm_increase_security 0x%04lx" USER_LOG_NL,
                      (unsigned long)sc);
        kc_smp_increase_done = true; // 不再重试
      }
    }
  }

}

void key_connect_init(void)
{
  sl_status_t sc;

  sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_SC_ONLY
                          | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED,
                          sl_bt_sm_io_capability_noinputnooutput);
  USER_LOG_INFO("[KEY_CONN] sm_configure(SC_ONLY+BondConfirm,NoIO) sc=0x%04lx" USER_LOG_NL,
                (unsigned long)sc);

  sc = sl_bt_sm_store_bonding_configuration(2, 0);
  USER_LOG_INFO("[KEY_CONN] sm_store_bonds(max=2) sc=0x%04lx" USER_LOG_NL,
                (unsigned long)sc);

  sl_bt_sm_set_bondable_mode(1);
  USER_LOG_INFO("[KEY_CONN] bondable_mode=1" USER_LOG_NL);

  ble_peer_manager_filter_init();
  USER_LOG_INFO("[KEY_CONN] filter init done" USER_LOG_NL);

  // 启用地址白名单过滤, 并从 NVM 加载已绑定设备地址
  ble_peer_manager_set_filter_bt_address(true);
  kc_dump_bonding_table();

  // 复位定时变量
  kc_connected_conn = SL_BT_INVALID_CONNECTION_HANDLE;
  kc_smp_increase_done = true;
  kc_smp_from_pairing = false;
  kc_pairing_deadline_ms = 0;
  kc_smp_increase_at_ms = 0;
  kc_smp_fallback_at_ms = 0;
  kc_scan_restart_cnt = 0;
  kc_normal_rpt_cnt = 0;

  // 自动进入 NORMAL_SCAN (此时白名单已配置)
  kc_enter_normal_scan();
}

void key_connect_start_scan(void)
{
  kc_enter_normal_scan();
}

void key_connect_open(const char *addr_str)
{
  bd_addr addr;
  sl_status_t sc = ble_peer_manager_str_to_address(addr_str, &addr);
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[KEY_CONN] invalid address: \"%s\"" USER_LOG_NL, addr_str);
    return;
  }
  USER_LOG_INFO("[KEY_CONN] connecting to %02X:%02X:%02X:%02X:%02X:%02X" USER_LOG_NL,
                addr.addr[5], addr.addr[4], addr.addr[3],
                addr.addr[2], addr.addr[1], addr.addr[0]);
  sc = ble_peer_manager_central_open_connection(&addr, sl_bt_gap_public_address);
  if (sc == SL_STATUS_OK)
    USER_LOG_INFO("[KEY_CONN] connection request sent" USER_LOG_NL);
  else
    USER_LOG_ERROR("[KEY_CONN] connect failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
}

void key_connect_disconnect(void)
{
  if (app_key_conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
    USER_LOG_INFO("[KEY_CONN] no active connection" USER_LOG_NL);
    return;
  }
  USER_LOG_INFO("[KEY_CONN] disconnecting handle=%u" USER_LOG_NL,
                (unsigned)app_key_conn_handle);
  sl_status_t sc = ble_peer_manager_central_close_connection(app_key_conn_handle);
  if (sc != SL_STATUS_OK)
    USER_LOG_ERROR("[KEY_CONN] disconnect failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
}

void key_connect_auto_connect(void)
{
  ble_peer_manager_reset_filter();
  ble_peer_manager_set_filter_bt_address(true);
  USER_LOG_INFO("[KEY_CONN] auto_connect: whitelist filter enabled" USER_LOG_NL);
  kc_dump_bonding_table();
  kc_enter_normal_scan();
}

void key_connect_add_whitelist(const char *addr_str)
{
  bd_addr addr;
  sl_status_t sc = ble_peer_manager_str_to_address(addr_str, &addr);
  if (sc != SL_STATUS_OK) {
    USER_LOG_ERROR("[KEY_CONN] wl_add: invalid address \"%s\"" USER_LOG_NL, addr_str);
    return;
  }
  sc = ble_peer_manager_add_allowed_bt_address(&addr);
  if (sc == SL_STATUS_OK)
    USER_LOG_INFO("[KEY_CONN] whitelist added: %02X:%02X:%02X:%02X:%02X:%02X" USER_LOG_NL,
                  addr.addr[5], addr.addr[4], addr.addr[3],
                  addr.addr[2], addr.addr[1], addr.addr[0]);
  else
    USER_LOG_ERROR("[KEY_CONN] wl_add failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
}

void key_connect_enter_pairing_mode(void)
{
  uint64_t now = kc_now_ms();

  sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_SC_ONLY
                     | SL_BT_SM_CONFIGURATION_BONDING_REQUEST_REQUIRED,
                     sl_bt_sm_io_capability_noinputnooutput);
  sl_bt_sm_set_bondable_mode(1);
  USER_LOG_INFO("[KEY_CONN] pair_mode: bondable=1" USER_LOG_NL);

  kc_state = KEY_CONN_PAIRING_SCAN;
  kc_pairing_deadline_ms = now + KC_PAIRING_WINDOW_MS;
  kc_pairing_remind_next_ms = now + KC_PAIRING_REMIND_MS;
  kc_scan_keepalive_next_ms = now + KC_SCAN_KEEPALIVE_MS;
  kc_diag_next_ms = now + KC_DIAG_INTERVAL_MS;
  kc_normal_rpt_cnt = 0;

  kc_scan_restart();
  USER_LOG_INFO("[KEY_CONN] PAIRING_SCAN: started (0xA5 filter, %lus window)" USER_LOG_NL,
                (unsigned long)(KC_PAIRING_WINDOW_MS / 1000));
}

// -----------------------------------------------------------------------------
// BLE 事件处理
// -----------------------------------------------------------------------------

void key_connect_on_bt_event(sl_bt_msg_t *evt)
{
  uint32_t evt_id = SL_BT_MSG_ID(evt->header);
  uint64_t now = kc_now_ms();

  switch (evt_id) {

    // ----- 系统启动 -----
    case sl_bt_evt_system_boot_id:
    {
      key_connect_init();
      break;
    }

    // ----- 扫描报告 (配对模式过滤) -----
    case sl_bt_evt_scanner_legacy_advertisement_report_id:
    {
      const sl_bt_evt_scanner_legacy_advertisement_report_t *rpt =
          &evt->data.evt_scanner_legacy_advertisement_report;

      if (kc_state == KEY_CONN_NORMAL_SCAN) {
        kc_normal_rpt_cnt++;
        break;
      }
      if (kc_state != KEY_CONN_PAIRING_SCAN) break;

      uint8_t adv_id;
      if (key_connect_parse_adv_data(rpt->data.data, rpt->data.len, &adv_id)) {
        USER_LOG_INFO("[KEY_CONN] pair adv: ID=0x%02X rssi=%d addr=%02X:%02X:%02X:%02X:%02X:%02X" USER_LOG_NL,
                      adv_id, rpt->rssi,
                      rpt->address.addr[5], rpt->address.addr[4], rpt->address.addr[3],
                      rpt->address.addr[2], rpt->address.addr[1], rpt->address.addr[0]);
        if (adv_id == 0xA5) {
          USER_LOG_INFO("[KEY_CONN] pair_mode: found 0xA5, connecting..." USER_LOG_NL);
          sl_status_t sc = ble_peer_manager_central_open_connection(
              (bd_addr *)&rpt->address, rpt->address_type);
          if (sc == SL_STATUS_OK) {
            kc_smp_from_pairing = true;
            USER_LOG_INFO("[KEY_CONN] pair_mode: connection request sent" USER_LOG_NL);
          } else {
            USER_LOG_ERROR("[KEY_CONN] pair_mode: connect failed 0x%04lx" USER_LOG_NL,
                           (unsigned long)sc);
          }
        }
      }
      break;
    }

    case sl_bt_evt_scanner_extended_advertisement_report_id:
    {
      const sl_bt_evt_scanner_extended_advertisement_report_t *rpt =
          &evt->data.evt_scanner_extended_advertisement_report;

      if (kc_state == KEY_CONN_NORMAL_SCAN) {
        kc_normal_rpt_cnt++;
        break;
      }
      if (kc_state != KEY_CONN_PAIRING_SCAN) break;

      uint8_t adv_id;
      if (key_connect_parse_adv_data(rpt->data.data, rpt->data.len, &adv_id)) {
        USER_LOG_INFO("[KEY_CONN] pair adv ext: ID=0x%02X rssi=%d" USER_LOG_NL,
                      adv_id, rpt->rssi);
        if (adv_id == 0xA5) {
          USER_LOG_INFO("[KEY_CONN] pair_mode: found 0xA5 ext, connecting..." USER_LOG_NL);
          sl_status_t sc = ble_peer_manager_central_open_connection(
              (bd_addr *)&rpt->address, rpt->address_type);
          if (sc == SL_STATUS_OK) {
            kc_smp_from_pairing = true;
            USER_LOG_INFO("[KEY_CONN] pair_mode: connection request sent" USER_LOG_NL);
          } else {
            USER_LOG_ERROR("[KEY_CONN] pair_mode: connect failed 0x%04lx" USER_LOG_NL,
                           (unsigned long)sc);
          }
        }
      }
      break;
    }

    // ----- 连接打开 -----
    case sl_bt_evt_connection_opened_id:
    {
      const sl_bt_evt_connection_opened_t *d = &evt->data.evt_connection_opened;
      if (d->role != sl_bt_connection_role_central) break;

      kc_state = KEY_CONN_CONNECTED;
      kc_connected_conn = d->connection;
      kc_pairing_deadline_ms = 0;
      kc_smp_increase_done = false;

      USER_LOG_INFO("[KEY_CONN] connection_opened: conn=%u addr=%02X:%02X:%02X:%02X:%02X:%02X bond=0x%02X" USER_LOG_NL,
                    d->connection,
                    d->address.addr[5], d->address.addr[4], d->address.addr[3],
                    d->address.addr[2], d->address.addr[1], d->address.addr[0],
                    d->bonding);

      // 调度 sm_increase_security
      if (kc_smp_from_pairing) {
        // 配对模式: 短延迟
        kc_smp_increase_at_ms = now + KC_SMP_DELAY_MS;
        kc_smp_fallback_at_ms = now + KC_SMP_FALLBACK_MS;
        USER_LOG_INFO("[KEY_CONN] SMP scheduled: delay=%lums fallback=%lums (pairing)" USER_LOG_NL,
                      (unsigned long)KC_SMP_DELAY_MS, (unsigned long)KC_SMP_FALLBACK_MS);
      } else if (d->bonding != SL_BT_INVALID_BONDING_HANDLE) {
        // 已绑定设备重连: 从属优先启动, 主机只做后备
        kc_smp_increase_at_ms = 0; // 等从属先启动
        kc_smp_fallback_at_ms = now + KC_SMP_FALLBACK_MS;
        USER_LOG_INFO("[KEY_CONN] SMP deferred: wait for slave-led, fallback=%lums" USER_LOG_NL,
                      (unsigned long)KC_SMP_FALLBACK_MS);
      } else {
        // 新连接 (非配对): 直接启动
        kc_smp_increase_at_ms = now + KC_SMP_DELAY_MS;
        kc_smp_fallback_at_ms = now + KC_SMP_FALLBACK_MS;
        USER_LOG_INFO("[KEY_CONN] SMP scheduled: delay=%lums (new conn)" USER_LOG_NL,
                      (unsigned long)KC_SMP_DELAY_MS);
      }
      break;
    }

    // ----- 连接关闭 -----
    case sl_bt_evt_connection_closed_id:
    {
      const sl_bt_evt_connection_closed_t *d = &evt->data.evt_connection_closed;
      USER_LOG_INFO("[KEY_CONN] connection_closed: conn=%u reason=0x%04X" USER_LOG_NL,
                    d->connection, d->reason);

      if (d->connection == kc_connected_conn) {
        kc_connected_conn = SL_BT_INVALID_CONNECTION_HANDLE;
        kc_smp_increase_done = true;
        kc_smp_from_pairing = false;
        kc_sm_bonded = false;
      }

      // 立即进入 NORMAL_SCAN 白名单自动重连
      kc_state = KEY_CONN_NORMAL_SCAN;
      kc_scan_keepalive_next_ms = now + KC_SCAN_KEEPALIVE_MS;
      kc_scan_restart();
      USER_LOG_INFO("[KEY_CONN] disconnected -> NORMAL_SCAN (whitelist auto-reconnect)" USER_LOG_NL);
      break;
    }

    // ----- SM 事件 -----
    case sl_bt_evt_sm_confirm_passkey_id:
    {
      uint8_t conn = evt->data.evt_sm_confirm_passkey.connection;
      bool allow;
      sl_status_t sc;
      if (conn != kc_connected_conn) {
        USER_LOG_INFO("[KEY_CONN] SM confirm_passkey ignore non-key conn=%u"
                      USER_LOG_NL, conn);
        break;
      }
      allow = kc_smp_from_pairing;
      USER_LOG_INFO("[KEY_CONN] SM confirm_passkey conn=%u pairingMode=%s"
                    USER_LOG_NL, conn, allow ? "Y" : "N");
      sc = sl_bt_sm_passkey_confirm(conn, allow ? 1U : 0U);
      USER_LOG_INFO("[KEY_CONN] SM confirm_passkey %s sc=0x%04lX"
                    USER_LOG_NL, allow ? "ACCEPT" : "REJECT",
                    (unsigned long)sc);
      if (!allow || sc != SL_STATUS_OK) {
        (void)ble_peer_manager_central_close_connection(conn);
      }
      break;
    }

    case sl_bt_evt_sm_confirm_bonding_id:
    {
      uint8_t conn = evt->data.evt_sm_confirm_bonding.connection;
      uint8_t existing_bond = evt->data.evt_sm_confirm_bonding.bonding_handle;
      bool allow;
      sl_status_t sc;
      if (conn != kc_connected_conn) {
        USER_LOG_INFO("[KEY_CONN] SM confirm_bonding ignore non-key conn=%u"
                      USER_LOG_NL, conn);
        break;
      }
      allow = kc_smp_from_pairing;
      USER_LOG_INFO("[KEY_CONN] SM confirm_bonding conn=%u existingBond=0x%02X pairingMode=%s"
                    USER_LOG_NL, conn, existing_bond,
                    allow ? "Y" : "N");
      sc = sl_bt_sm_bonding_confirm(conn, allow ? 1U : 0U);
      USER_LOG_INFO("[KEY_CONN] SM confirm_bonding %s sc=0x%04lX"
                    USER_LOG_NL, allow ? "ACCEPT" : "REJECT",
                    (unsigned long)sc);
      if (!allow || sc != SL_STATUS_OK) {
        (void)ble_peer_manager_central_close_connection(conn);
      }
      break;
    }

    case sl_bt_evt_sm_bonded_id:
    {
      uint8_t conn = evt->data.evt_sm_bonded.connection;
      uint8_t bond_h = evt->data.evt_sm_bonded.bonding;
      if (conn != kc_connected_conn) {
        USER_LOG_INFO("[KEY_CONN] SM BONDED ignore non-key conn=%u" USER_LOG_NL,
                      conn);
        break;
      }
      kc_smp_increase_done = true;
      kc_smp_from_pairing = false;
      kc_sm_bonded = true;
      USER_LOG_INFO("[KEY_CONN] SM BONDED conn=%u bond_handle=0x%02X" USER_LOG_NL, conn, bond_h);
      USER_LOG_INFO("[KEY_CONN] === PAIRING + BONDING COMPLETE ===" USER_LOG_NL);
      break;
    }

    case sl_bt_evt_sm_bonding_failed_id:
    {
      uint8_t conn = evt->data.evt_sm_bonding_failed.connection;
      uint16_t reason = evt->data.evt_sm_bonding_failed.reason;
      if (conn != kc_connected_conn) {
        USER_LOG_INFO("[KEY_CONN] SM BONDING FAILED ignore non-key conn=%u"
                      USER_LOG_NL, conn);
        break;
      }
      kc_smp_increase_done = true;
      kc_smp_from_pairing = false;
      kc_sm_bonded = false;
      USER_LOG_INFO("[KEY_CONN] SM BONDING FAILED conn=%u reason=0x%04X" USER_LOG_NL,
                    conn, reason);
      break;
    }

    default:
      break;
  }
}

void key_connect_on_peer_manager_event(ble_peer_manager_evt_type_t *event)
{
  if (event == NULL) return;

  switch (event->evt_id) {
    case BLE_PEER_MANAGER_ON_CONN_OPENED_CENTRAL:
      USER_LOG_INFO("[KEY_CONN] PM: connected (handle=%u)" USER_LOG_NL,
                    (unsigned)event->connection_id);
      break;

    case BLE_PEER_MANAGER_ON_CONN_CLOSED:
      USER_LOG_INFO("[KEY_CONN] PM: disconnected (handle=%u)" USER_LOG_NL,
                    (unsigned)event->connection_id);
      break;

    default:
      break;
  }
}

// BLE 连接状态查询 (供协议模块 getter 使用)
bool key_connect_is_connected(void)
{
  return (app_key_conn_handle != SL_BT_INVALID_CONNECTION_HANDLE);
}

bool key_connect_is_bonded(void)
{
  if (kc_sm_bonded) return true;
  // 重连场景: SM_BONDED 事件不触发，主动查询安全模式
  uint8_t conn = app_key_conn_handle;
  if (conn == SL_BT_INVALID_CONNECTION_HANDLE) return false;
  uint8_t sec_mode = 0, key_size = 0, bond_h = 0;
  sl_status_t sc = sl_bt_connection_get_security_status(conn, &sec_mode, &key_size, &bond_h);
  return (sc == SL_STATUS_OK && sec_mode > (uint8_t)sl_bt_connection_mode1_level1);
}
