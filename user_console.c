#include "user_console.h"

#include "sl_string.h"
#include <string.h>
#include "user_log_console.h"
#include "app.h"
#include "app_config.h"
#include "ble_peer_manager_central.h"
#include "user_phone/phone_comm.h"
#include "user_phone/phone_cfg.h"
#include "user_phone/data/phone_storage.h"
#include "user_phone/data/phone_rang.h"
#include "user_hid/hid_service.h"
#include "sl_bt_api.h"
#include "user_app_fun/user_app_phone_peps.h"
#include "user_eeprom/user_eeprom.h"
#include "user_can_common/CanMatrix/CanMatrix_Cfg.h"
#include "user_can_common/RteSys.h"
#include "user_can_common/CanNm/CanNm.h"
#include "user_can_common/CanManage/CanManage.h"
#include "user_can_common/user_can_config.h"
#include "em_chip.h"

#if APP_KEY_ENABLE
#include "cs_key_rang/cs_key_rang.h"
#include "cs_key_rang/cs_key_console.h"
#include "key_connect/connect_console.h"
#include "key_connect/key_connect.h"
#include "key_connect/key_gatt_cmd.h"
#endif



/* EEPROM item name table (generated from user_eeprom_items.def) */
#define EEPROM_ITEM_X(name, addr, dlen, label, flags) label,
static const char *const g_eeprom_item_names[] = {
  #include "user_eeprom/user_eeprom_items.def"
};
#undef EEPROM_ITEM_X

/* ========== ����: ���� int8_t (֧�ָ���, �� "-55" -> -55) ========== */
static bool console_parse_int8(const char *s, int8_t *out)
{
  bool neg = false;
  bool has_digit = false;
  if (s == NULL || *s == '\0') return false;
  if (*s == '-') { neg = true; s++; }
  int32_t val = 0;
  while (*s != '\0' && *s != ' ') {
    if (*s < '0' || *s > '9') return false;
    val = val * 10 + (int32_t)(*s - '0');
    if (val > 128) return false;
    has_digit = true;
    s++;
  }
  if (!has_digit) return false;
  if (neg) val = -val;
  if (val < -128 || val > 127) return false;
  *out = (int8_t)val;
  return true;
}
bool app_user_log_on_command(const char *line, void *context)
{
  sl_status_t sc;
  (void)context;

  if (sl_strcasecmp(line, "help") == 0) {
    USER_LOG_INFO(
      "=== UART commands (case-insensitive) ===\r\n"
      "  help           - show this list\r\n"
#if APP_KEY_ENABLE
      "  status         - connection / ranging state\r\n"
      "  rang_start     - start CS ranging on active connection\r\n"
      "  rang_stop      - stop CS ranging\r\n"
      "  rang_toggle    - start or stop ranging\r\n"
      "  rang_result    - print latest measurement\r\n"
      "  scan           - restart central scan for reflector\r\n"
      "  disconnect     - close current connection\r\n"
      "  auto_conn      - start scan with whitelist filter\r\n"
            /* [Key] reset bonds + proto NVM */
      "  key_reset      - factory reset key bonds + NVM\r\n"
      /* [Key] enter pairing scan */
      "  key_pair       - enter pairing mode (0xA5)\r\n"
      /* [Key] show proto trace/security state */
      "  key_proto      - show protocol phase/security state\r\n"
#endif
      );
    USER_LOG_INFO(
      "=== Phone Commands ===\r\n"
      "  phone_status         - connection / auth state\r\n"
      "  phone_lock <0|1|2>   - simulate vehicle lock (0=UNKNOWN 1=LOCKED 2=UNLOCKED)\r\n"
      "  phone_auth_cond <0|1>- simulate PEPS auth condition (0=NOT_MET 1=MET)\r\n"
      "  phone_adv_primary    - primary advertising (0x5A)\r\n"
      "  phone_adv_secondary  - learn advertising (0xA5)\r\n"
      "  phone_disconnect     - disconnect phone\r\n"
      "  phone_did            - show deviceId\r\n"
      "  phone_provision      - show provision state\r\n"
      "  phone_qid <str>      - write qid (max 32 chars)\r\n"
      "  phone_cv <num>       - write cv (U32, >0)\r\n"
      "  phone_bind_secret <hex32> - write bindSecret (16 Byte hex)\r\n"
      "  phone_factory_pubkey <hex130> - write factory pubkey (65 Byte hex)\r\n"
      "  reset               - software reset MCU\r\n"
      "  eeprom_dump         - dump all EEPROM items\r\n"
      "  phone_unbind        - unbind (delete provision + factory prov)\r\n"
      "  phone_rang          - RSSI ranging: cal params + realtime RSSI + fused dist\r\n"
      "  phone_rang_cal <rssi_1m> <rssi_10m> - auto cal at 1m/10m, save to NVM\r\n"
      "  phone_rang_cal_reset     - reset cal to defaults\r\n"
      "  hid_cfg <0|1|2>     - HID mode: 0=disable 1=enable(reconnect) 2=enable(pairing,clear bond); immediate + EE\r\n"
      "  hid_pin <6digits|0>  - fixed debug pairing PIN (0=random); immediate + EE\r\n"
      "  hid_status           - HID runtime + config state\r\n"
      "  hid_adv              - enable HID adv + refresh now (for scan)\r\n");
    USER_LOG_INFO(
      "=== Phone PEPS Zone ===\r\n"
      "  phone_zone              - zone state: connection + distance + 6 thresholds + sensitivity\r\n"
      "  phone_zone_th <0-5> <cm> - set zone threshold (saved to EEPROM)\r\n"
      "  phone_zone_th_reset     - reset thresholds to factory defaults\r\n"
      "  phone_sens_th <1-3> <cm> - set sensitivity near/std/far unlock distance (saved to EEPROM)\r\n"
      "  phone_sens_th_reset     - reset sensitivity distances to defaults\r\n");
    USER_LOG_INFO(
      "=== CAN NM Commands ===\r\n"
      "  can_nm_sleep    - force request network sleep (manual latch, auto-sleep skipped)\r\n"
      "  can_nm_wake     - clear sleep request (software/local wake; auto-sleep resumes)\r\n"
      "  can_nm_status   - show CanManage mode + CanNm state + sleep flags\r\n");
    return true;
  }

#if APP_KEY_ENABLE
  if (sl_strcasecmp(line, "status") == 0) {
    if (app_key_conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
      USER_LOG_INFO("[UART] status: no reflector connection (CS disabled)" USER_LOG_NL);
    } else {
      USER_LOG_INFO("[UART] status: conn=%u ready=%s ranging=%s new_result=%s" USER_LOG_NL,
                    app_key_conn_handle,
                    cs_key_rang_is_connection_ready(app_key_conn_handle) ? "yes" : "no",
                    cs_key_rang_is_ranging(app_key_conn_handle) ? "yes" : "no",
                    cs_key_rang_has_new_result(app_key_conn_handle) ? "yes" : "no");
    }
    return true;
  }

  if (sl_strcasecmp(line, "rang_start") == 0) {
    if (app_key_conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
      USER_LOG_INFO("[UART] rang_start: no reflector connection" USER_LOG_NL);
      return true;
    }
    sc = cs_key_rang_start(app_key_conn_handle);
    if (sc == SL_STATUS_OK) {
      USER_LOG_INFO("[UART] rang_start: started (conn %u)" USER_LOG_NL, app_key_conn_handle);
    } else if (sc == SL_STATUS_IN_PROGRESS) {
      USER_LOG_INFO("[UART] rang_start: pending (wait for CS teardown)" USER_LOG_NL);
    } else {
      USER_LOG_ERROR("[UART] rang_start failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
    }
    return true;
  }

  if (sl_strcasecmp(line, "rang_stop") == 0) {
    if (app_key_conn_handle == SL_BT_INVALID_CONNECTION_HANDLE) {
      USER_LOG_INFO("[UART] rang_stop: no reflector connection" USER_LOG_NL);
      return true;
    }
    sc = cs_key_rang_stop(app_key_conn_handle);
    if (sc == SL_STATUS_OK) {
      USER_LOG_INFO("[UART] rang_stop: stopped (conn %u)" USER_LOG_NL, app_key_conn_handle);
    } else {
      USER_LOG_ERROR("[UART] rang_stop failed 0x%04lx" USER_LOG_NL, (unsigned long)sc);
    }
    return true;
  }

  if (sl_strcasecmp(line, "rang_toggle") == 0) {
    app_handle_toggle_ranging();
    return true;
  }

  if (sl_strcasecmp(line, "rang_result") == 0) {
    app_handle_print_result();
    return true;
  }

  if (sl_strcasecmp(line, "scan") == 0) {
    key_connect_start_scan();
    return true;
  }

  if (sl_strcasecmp(line, "bond_init") == 0) {
    key_connect_init();
    return true;
  }

  if (sl_strcasecmp(line, "connect ") == 0) {
    // key_connect_open("98de4456bcd8");
    return true;
  }

  if (sl_strcasecmp(line, "disconnect") == 0) {
    key_connect_disconnect();
    return true;
  }

  if (sl_strcasecmp(line, "auto_conn") == 0) {
    key_connect_auto_connect();
    return true;
  }

  if (sl_strcasecmp(line, "wl_add ") == 0) {
    key_connect_add_whitelist(line + 7);
    return true;
  }

  if (sl_strcasecmp(line, "key_pair") == 0) {
    key_gatt_proto_reset_pairing();
    key_connect_enter_pairing_mode();
    return true;
  }

  if (sl_strcasecmp(line, "key_proto") == 0) {
    USER_LOG_INFO("[UART] key_proto: is_phase_c=%d" USER_LOG_NL,
                  key_gatt_proto_is_in_phase_c());
    return true;
  }

  if (sl_strcasecmp(line, "key_reset") == 0) {
    app_ble_factory_reset_bonds_key();
    return true;
  }
#endif /* APP_KEY_ENABLE */

  // --- �ֻ�ͨ������ ---

  if (sl_strcasecmp(line, "phone_status") == 0) {
    int8_t rssi = 0;
    bool rssi_ok = phone_rang_get_local_rssi(&rssi);
    USER_LOG_INFO("[UART] phone: conn=%s hid=%s notify=%s auth=%s passive=%s rssi=%d%s" USER_LOG_NL,
                  phone_comm_is_connected() ? "Y" : "N",
                  hid_service_is_connected() ? "Y" : "N",
                  phone_comm_is_notify_enabled() ? "Y" : "N",
                  phone_comm_is_authenticated() ? "Y" : "N",
                  phone_comm_is_passive_enabled() ? "Y" : "N",
                  (int)rssi, rssi_ok ? "" : "(N/A)");
    return true;
  }

  if (sl_strcasecmp(line, "phone_adv_stop") == 0) {
    USER_LOG_INFO("[UART] phone_adv_stop: use phone_disconnect to stop adv" USER_LOG_NL);
    return true;
  }

  if (sl_strcasecmp(line, "phone_adv_start") == 0) {
    USER_LOG_INFO("[UART] phone_adv_start: advertising auto-recovers after disconnect" USER_LOG_NL);
    return true;
  }

  if (sl_strcasecmp(line, "phone_adv_secondary") == 0) {
    phone_comm_switch_to_secondary();
    USER_LOG_INFO("[UART] phone_adv_secondary: switched to 0xA5" USER_LOG_NL);
    return true;
  }

  if (sl_strcasecmp(line, "phone_adv_primary") == 0) {
    phone_comm_switch_to_primary();
    USER_LOG_INFO("[UART] phone_adv_primary: switched to 0x5A" USER_LOG_NL);
    return true;
  }

  if (sl_strcasecmp(line, "phone_disconnect") == 0) {
    uint8_t handle = phone_comm_connection_handle_get();
    if (handle == SL_BT_INVALID_CONNECTION_HANDLE) {
      USER_LOG_INFO("[UART] phone_disconnect: not connected" USER_LOG_NL);
    } else {
      sl_bt_connection_close(handle);
      USER_LOG_INFO("[UART] phone_disconnect: closing connection %u" USER_LOG_NL, handle);
    }
    return true;
  }

  /* ---- �������ݶ�д ---- */
  {
    const char *arg = NULL;
    size_t cmd_len;

    /* ������: ��Сд������ǰ׺ƥ��, arg ָ�������ʼ */
    #define CMD_PREFIX(line_, cmd_, arg_)                               \
      (arg = NULL, cmd_len = strlen(cmd_),                              \
       ({ bool _m = true;                                               \
          for (size_t _i = 0; _i < cmd_len; _i++) {                    \
            char _l = (const char)((line_[_i] >= 'A' && line_[_i] <= 'Z') ? (line_[_i] + 32) : line_[_i]); \
            char _r = cmd_[_i];                                         \
            if (_l != _r) { _m = false; break; }                        \
          }                                                             \
          if (_m && line_[cmd_len] != '\0' && line_[cmd_len] != ' ') _m = false; \
          if (_m) {                                                     \
            const char *_a = line_ + cmd_len;                           \
            while (*_a == ' ') _a++;                                    \
            arg = _a;                                                   \
          }                                                             \
          _m; }))

    if (sl_strcasecmp(line, "phone_provision") == 0) {
      char qid_buf[33] = {0};
      uint32_t cv = phone_storage_get_cv();
      sl_status_t rsc = phone_storage_get_qid(qid_buf, sizeof(qid_buf));
      bool cv_written = user_eeprom_is_valid(EEPROM_PHONE_CV);
      uint8_t secret[16] = {0};
      sl_status_t sec_sc = phone_storage_get_bind_secret(secret);
      USER_LOG_INFO("[UART] phone_provision:" USER_LOG_NL);
      USER_LOG_INFO("  qid        = %s (sc=0x%04lX)" USER_LOG_NL,
                    (rsc == SL_STATUS_OK && qid_buf[0] != '\0') ? qid_buf : "(empty)", (unsigned long)rsc);
      USER_LOG_INFO("  cv         = %lu / 0x%08lX (nvm_written=%s)" USER_LOG_NL,
                    (unsigned long)cv, (unsigned long)cv, cv_written ? "yes" : "no");
      if (sec_sc == SL_STATUS_OK) {
        USER_LOG_INFO("  bindSecret = %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X (16 Byte)" USER_LOG_NL,
                      (unsigned)secret[0], (unsigned)secret[1], (unsigned)secret[2], (unsigned)secret[3],
                      (unsigned)secret[4], (unsigned)secret[5], (unsigned)secret[6], (unsigned)secret[7],
                      (unsigned)secret[8], (unsigned)secret[9], (unsigned)secret[10], (unsigned)secret[11],
                      (unsigned)secret[12], (unsigned)secret[13], (unsigned)secret[14], (unsigned)secret[15]);
      } else {
        USER_LOG_INFO("  bindSecret = (empty, sc=0x%04lX)" USER_LOG_NL, (unsigned long)sec_sc);
      }
      return true;
    }

    if (CMD_PREFIX(line, "phone_qid", arg)) {
      if (*arg == '\0') {
        USER_LOG_INFO("[UART] phone_qid usage: phone_qid <string> (max 32 chars) e.g. phone_qid QR_0001" USER_LOG_NL);
        return true;
      }
      uint16_t len = (uint16_t)strlen(arg);
      if (len > PHONE_TLV_MAX_QID) {
        USER_LOG_INFO("[UART] phone_qid: too long (%u > %u)" USER_LOG_NL, (unsigned)len, (unsigned)PHONE_TLV_MAX_QID);
        return true;
      }
      sl_status_t wr_sc = phone_storage_set_qid(arg);
      if (wr_sc == SL_STATUS_OK) {
        USER_LOG_INFO("[UART] phone_qid: written '%s'" USER_LOG_NL, arg);
      } else {
        USER_LOG_ERROR("[UART] phone_qid: write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)wr_sc);
      }
      return true;
    }

    if (CMD_PREFIX(line, "phone_cv", arg)) {
      if (*arg == '\0') {
        USER_LOG_INFO("[UART] phone_cv usage: phone_cv <number> (U32, >0) e.g. phone_cv 1" USER_LOG_NL);
        return true;
      }
      unsigned long val = 0;
      {
        const char *p = arg;
        while (*p >= '0' && *p <= '9') { val = val * 10 + (unsigned long)(*p - '0'); p++; }
        if (p == arg || *p != '\0') {
          USER_LOG_INFO("[UART] phone_cv: invalid number '%s'" USER_LOG_NL, arg);
          return true;
        }
      }
      if (val == 0 || val > 0xFFFFFFFFUL) {
        USER_LOG_INFO("[UART] phone_cv: out of range (1 .. 0xFFFFFFFF)" USER_LOG_NL);
        return true;
      }
      sl_status_t wr_sc = phone_storage_set_cv((uint32_t)val);
      if (wr_sc == SL_STATUS_OK) {
        USER_LOG_INFO("[UART] phone_cv: written %lu (0x%08lX)" USER_LOG_NL, val, val);
      } else {
        USER_LOG_ERROR("[UART] phone_cv: write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)wr_sc);
      }
      return true;
    }

    if (CMD_PREFIX(line, "phone_bind_secret", arg)) {
      if (*arg == '\0') {
        USER_LOG_INFO("[UART] phone_bind_secret usage: phone_bind_secret <32 hex chars> e.g. phone_bind_secret 00112233445566778899AABBCCDDEEFF" USER_LOG_NL);
        return true;
      }
      uint8_t secret[16];
      if (strlen(arg) != 32) {
        USER_LOG_INFO("[UART] phone_bind_secret: need exactly 32 hex chars, got %u" USER_LOG_NL, (unsigned)strlen(arg));
        return true;
      }
      {
        unsigned i;
        for (i = 0; i < 16; i++) {
          char hi = arg[i * 2], lo = arg[i * 2 + 1];
          uint8_t nib_hi, nib_lo;
          if (hi >= '0' && hi <= '9') nib_hi = (uint8_t)(hi - '0');
          else if (hi >= 'A' && hi <= 'F') nib_hi = (uint8_t)(hi - 'A' + 10);
          else if (hi >= 'a' && hi <= 'f') nib_hi = (uint8_t)(hi - 'a' + 10);
          else { USER_LOG_INFO("[UART] phone_bind_secret: invalid hex char '%c'" USER_LOG_NL, hi); return true; }
          if (lo >= '0' && lo <= '9') nib_lo = (uint8_t)(lo - '0');
          else if (lo >= 'A' && lo <= 'F') nib_lo = (uint8_t)(lo - 'A' + 10);
          else if (lo >= 'a' && lo <= 'f') nib_lo = (uint8_t)(lo - 'a' + 10);
          else { USER_LOG_INFO("[UART] phone_bind_secret: invalid hex char '%c'" USER_LOG_NL, lo); return true; }
          secret[i] = (uint8_t)((nib_hi << 4) | nib_lo);
        }
      }
      sl_status_t wr_sc = phone_storage_set_bind_secret(secret);
      if (wr_sc == SL_STATUS_OK) {
        USER_LOG_INFO("[UART] phone_bind_secret: written (16 Byte)" USER_LOG_NL);
      } else {
        USER_LOG_ERROR("[UART] phone_bind_secret: write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)wr_sc);
      }
      return true;
    }

    if (CMD_PREFIX(line, "phone_factory_pubkey", arg)) {
      if (*arg == '\0') {
        USER_LOG_INFO("[UART] phone_factory_pubkey usage: phone_factory_pubkey <130 hex chars> (65 Byte ECDSA P-256 pubkey, starting with 04)" USER_LOG_NL);
        return true;
      }
      if (strlen(arg) != 130) {
        USER_LOG_INFO("[UART] phone_factory_pubkey: need exactly 130 hex chars (65 bytes), got %u" USER_LOG_NL, (unsigned)strlen(arg));
        return true;
      }
      uint8_t pubkey[65];
      {
        unsigned i;
        for (i = 0; i < 65; i++) {
          char hi = arg[i * 2], lo = arg[i * 2 + 1];
          uint8_t nib_hi, nib_lo;
          if (hi >= '0' && hi <= '9') nib_hi = (uint8_t)(hi - '0');
          else if (hi >= 'A' && hi <= 'F') nib_hi = (uint8_t)(hi - 'A' + 10);
          else if (hi >= 'a' && hi <= 'f') nib_hi = (uint8_t)(hi - 'a' + 10);
          else { USER_LOG_INFO("[UART] phone_factory_pubkey: invalid hex char '%c' at pos %u" USER_LOG_NL, hi, (unsigned)(i*2)); return true; }
          if (lo >= '0' && lo <= '9') nib_lo = (uint8_t)(lo - '0');
          else if (lo >= 'A' && lo <= 'F') nib_lo = (uint8_t)(lo - 'A' + 10);
          else if (lo >= 'a' && lo <= 'f') nib_lo = (uint8_t)(lo - 'a' + 10);
          else { USER_LOG_INFO("[UART] phone_factory_pubkey: invalid hex char '%c' at pos %u" USER_LOG_NL, lo, (unsigned)(i*2+1)); return true; }
          pubkey[i] = (uint8_t)((nib_hi << 4) | nib_lo);
        }
      }
      if (pubkey[0] != 0x04) {
        USER_LOG_INFO("[UART] phone_factory_pubkey: WARNING - first byte is 0x%02X, expected 0x04 (uncompressed point)" USER_LOG_NL, (unsigned)pubkey[0]);
        /* ����ֹд��, ������ */
      }
      sl_status_t wr_sc = phone_storage_set_factory_pubkey(pubkey);
      if (wr_sc == SL_STATUS_OK) {
        USER_LOG_INFO("[UART] phone_factory_pubkey: written (65 Byte). First 8 bytes: %02X %02X %02X %02X %02X %02X %02X %02X" USER_LOG_NL,
                      (unsigned)pubkey[0], (unsigned)pubkey[1], (unsigned)pubkey[2], (unsigned)pubkey[3],
                      (unsigned)pubkey[4], (unsigned)pubkey[5], (unsigned)pubkey[6], (unsigned)pubkey[7]);
      } else {
        USER_LOG_ERROR("[UART] phone_factory_pubkey: write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)wr_sc);
      }
      return true;
    }

    /* ---- phone_rang: RSSI �������״̬ ---- */
    if (sl_strcasecmp(line, "phone_rang") == 0) {
      bool r1_ok, r10_ok, local_ok, remote_ok, dist_ok;
      int8_t r1, r10, rssi_local, rssi_remote;
      float n, dist_m;
      int n_x10, dist_cm;

      r1_ok  = user_eeprom_is_valid(EEPROM_PHONE_RANG_CAL_RSSI_1M);
      r10_ok = user_eeprom_is_valid(EEPROM_PHONE_RANG_CAL_RSSI_10M);
      phone_rang_get_calibration(&r1, &r10, &n);
      n_x10 = (int)(n * 10.0f + 0.5f);

      rssi_local = 0; rssi_remote = 0;
      local_ok  = phone_rang_get_local_rssi(&rssi_local);
      remote_ok = phone_rang_get_remote_rssi(&rssi_remote);

      dist_m = 0.0f;
      dist_ok = phone_rang_get_distance(&dist_m);

      USER_LOG_INFO("[UART] === Phone RSSI Ranging ===" USER_LOG_NL);
      USER_LOG_INFO("  cal: rssi_1m=%d(%s) rssi_10m=%d(%s) n=%d.%d" USER_LOG_NL,
                    (int)r1, r1_ok ? "NVM" : "DEFAULT",
                    (int)r10, r10_ok ? "NVM" : "DEFAULT",
                    n_x10 / 10, n_x10 % 10);
      USER_LOG_INFO("  local RSSI:  %s%d dBm" USER_LOG_NL,
                    local_ok ? "" : "N/A ", local_ok ? (int)rssi_local : 0);
      USER_LOG_INFO("  remote RSSI: %s%d dBm" USER_LOG_NL,
                    remote_ok ? "" : "N/A ", remote_ok ? (int)rssi_remote : 0);
      if (dist_ok) {
        dist_cm = (int)(dist_m * 100.0f + 0.5f);
        USER_LOG_INFO("  fused dist:  %d.%02d m (valid)" USER_LOG_NL,
                      dist_cm / 100, dist_cm % 100);
      } else {
        USER_LOG_INFO("  fused dist:  -- (invalid)" USER_LOG_NL);
      }
      return true;
    }

    if (CMD_PREFIX(line, "phone_rang_cal", arg)) {
      if (*arg == '\0') {
        USER_LOG_INFO("[UART] phone_rang_cal usage: phone_rang_cal <rssi_1m> <rssi_10m> (e.g. -55 -80)" USER_LOG_NL);
        USER_LOG_INFO("[UART]   �� auto-calculates tx_1m=rssi_1m, n=(rssi_1m-rssi_10m)/10" USER_LOG_NL);
        return true;
      }
      /* �������� int8_t ���� (�ո�ָ�, �� "-55 -80") */
      int8_t rssi_vals[2] = {0, 0};
      bool ok = true;
      {
        const char *p = arg;
        int vi;
        for (vi = 0; vi < 2; vi++) {
          /* ����ǰ���ո� */
          while (*p == ' ') p++;
          if (*p == '\0') { ok = false; break; }
          if (!console_parse_int8(p, &rssi_vals[vi])) { ok = false; break; }
          /* �ƶ�����һ���ո���β */
          while (*p != ' ' && *p != '\0') p++;
        }
        /* ȷ��û�ж������ */
        while (*p == ' ') p++;
        if (*p != '\0') ok = false;
      }
      if (!ok) {
        USER_LOG_INFO("[UART] phone_rang_cal: invalid �� need exactly 2 int8 values, e.g. -55 -80" USER_LOG_NL);
        return true;
      }
      if (rssi_vals[0] <= rssi_vals[1]) {
        USER_LOG_INFO("[UART] phone_rang_cal: invalid �� rssi_1m(%d) must be > rssi_10m(%d)" USER_LOG_NL,
                      (int)rssi_vals[0], (int)rssi_vals[1]);
        return true;
      }
      phone_rang_cal_auto(rssi_vals[0], rssi_vals[1]);
      return true;
    }

    if (sl_strcasecmp(line, "phone_rang_cal_reset") == 0) {
      phone_rang_clear_calibration();
      USER_LOG_INFO("[UART] phone_rang_cal_reset: calibration cleared, using defaults" USER_LOG_NL);
      return true;
    }

    #undef CMD_PREFIX
  }

  /* ---- phone_zone: �ֻ� PEPS λ����״̬ ---- */
  if (sl_strcasecmp(line, "phone_zone") == 0) {
    {
      static const char *const zone_names[] = {
        "δ֪", "����", "������", "������", "��Ч��"
      };
      static const char *const th_names[] = {
        "in_enter", "in_exit", "unlock_enter", "unlock_exit", "invalid_enter", "invalid_exit"
      };
      uint8_t zone = user_app_phone_peps_get_zone();
      float dist;
      bool dist_ok;
      int i;

      dist = user_app_phone_peps_get_distance();
      dist_ok = user_app_phone_peps_is_distance_valid();

      USER_LOG_INFO("[UART] === Phone PEPS Zone ===" USER_LOG_NL);
      USER_LOG_INFO("  zone: %s(%d)  dist: %s%.2fm" USER_LOG_NL,
                    (zone < 5U) ? zone_names[zone] : "?",
                    (int)zone,
                    dist_ok ? "" : "N/A ",
                    dist_ok ? (double)dist : 0.0);
      USER_LOG_INFO("  Thresholds (cm):" USER_LOG_NL);
      for (i = 0; i < 6; i++) {
        uint16_t val = user_app_phone_peps_threshold_get((uint8_t)i);
        bool from_eeprom = user_app_phone_peps_threshold_is_from_eeprom((uint8_t)i);
        USER_LOG_INFO("    [%d] %-14s = %4u (%s)" USER_LOG_NL,
                      i, th_names[i], (unsigned)val,
                      from_eeprom ? "NVM" : "DEFAULT");
      }

      /* 三档灵敏度走近解锁距离 (当前档位标 active) */
      {
        static const char *const sens_names[] = { "near", "std", "far" };
        uint8_t sens_level = phone_comm_get_passive_sensitivity();
        bool sens_nvm = user_app_phone_peps_sens_dist_is_from_eeprom();
        int si;
        USER_LOG_INFO("  Sensitivity: %u (%s)" USER_LOG_NL,
                      (unsigned)sens_level,
                      (sens_level >= 1U && sens_level <= 3U) ? sens_names[sens_level - 1U] : "?");
        USER_LOG_INFO("  Sens distances (cm): (%s)" USER_LOG_NL,
                      sens_nvm ? "NVM" : "DEFAULT");
        for (si = 1; si <= 3; si++) {
          uint16_t sd = user_app_phone_peps_sens_dist_get((uint8_t)si);
          USER_LOG_INFO("    [%d] %-6s = %4u %s" USER_LOG_NL, si, sens_names[si - 1],
                        (unsigned)sd,
                        ((uint8_t)si == sens_level) ? "(active)" : "");
        }
        USER_LOG_INFO("  active unlock-enter = %u cm" USER_LOG_NL,
                      (unsigned)user_app_phone_peps_sens_dist_effective());
      }
    }
    return true;
  }

  /* ---- phone_zone_th: ���õ�����ֵ ---- */
  if (strncmp(line, "phone_zone_th ", 14) == 0) {
    const char *p = line + 14;
    unsigned long idx, val;

    /* �����ո� */
    while (*p == ' ') p++;

    /* ���� index */
    if (*p < '0' || *p > '5') {
      USER_LOG_INFO("[UART] phone_zone_th: invalid index '%c' (need 0-5)" USER_LOG_NL, *p);
      USER_LOG_INFO("[UART]   0=in_enter 1=in_exit 2=unlock_enter 3=unlock_exit 4=invalid_enter 5=invalid_exit" USER_LOG_NL);
      return true;
    }
    idx = (unsigned long)(*p - '0');
    p++;
    while (*p == ' ') p++;

    /* ���� value */
    if (*p < '0' || *p > '9') {
      USER_LOG_INFO("[UART] phone_zone_th usage: phone_zone_th <0-5> <cm> (e.g. phone_zone_th 3 850)" USER_LOG_NL);
      return true;
    }
    val = 0;
    while (*p >= '0' && *p <= '9') {
      val = val * 10UL + (unsigned long)(*p - '0');
      if (val > 3000UL) {
        USER_LOG_INFO("[UART] phone_zone_th: value too large (>3000cm)" USER_LOG_NL);
        return true;
      }
      p++;
    }
    if (val < 10UL) {
      USER_LOG_INFO("[UART] phone_zone_th: value too small (<10cm)" USER_LOG_NL);
      return true;
    }

    /* ��ȡ��ֵ */
    {
      uint16_t old_val = user_app_phone_peps_threshold_get((uint8_t)idx);
      static const char *const th_names[] = {
        "in_enter", "in_exit", "unlock_enter", "unlock_exit", "invalid_enter", "invalid_exit"
      };

      /* д����ֵ */
      user_app_phone_peps_threshold_set((uint8_t)idx, (uint16_t)val);

      USER_LOG_INFO("[UART] phone_zone_th: [%lu] %s %u -> %lu cm (EEPROM written)" USER_LOG_NL,
                    idx, th_names[idx], (unsigned)old_val, val);

      /* ��ѡ: У����ͷ��� */
      if (idx == 0U) {
        uint16_t exit_val = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_IN_EXIT);
        if ((uint16_t)val >= exit_val) {
          USER_LOG_INFO("[UART]   WARNING: in_enter(%lu) >= in_exit(%u), ���ͷ����쳣!" USER_LOG_NL,
                        val, (unsigned)exit_val);
        }
      } else if (idx == 1U) {
        uint16_t enter_val = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_IN_ENTER);
        if (enter_val >= (uint16_t)val) {
          USER_LOG_INFO("[UART]   WARNING: in_enter(%u) >= in_exit(%lu), ���ͷ����쳣!" USER_LOG_NL,
                        (unsigned)enter_val, val);
        }
      } else if (idx == 2U) {
        uint16_t exit_val = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_UNLOCK_EXIT);
        if ((uint16_t)val >= exit_val) {
          USER_LOG_INFO("[UART]   WARNING: unlock_enter(%lu) >= unlock_exit(%u), ���ͷ����쳣!" USER_LOG_NL,
                        val, (unsigned)exit_val);
        }
      } else if (idx == 3U) {
        uint16_t enter_val = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_UNLOCK_ENTER);
        if (enter_val >= (uint16_t)val) {
          USER_LOG_INFO("[UART]   WARNING: unlock_enter(%u) >= unlock_exit(%lu), ���ͷ����쳣!" USER_LOG_NL,
                        (unsigned)enter_val, val);
        }
      } else if (idx == 4U) {
        uint16_t exit_val = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_INVALID_EXIT);
        if ((uint16_t)val <= exit_val) {
          USER_LOG_INFO("[UART]   WARNING: invalid_enter(%lu) <= invalid_exit(%u), ���ͷ����쳣!" USER_LOG_NL,
                        val, (unsigned)exit_val);
        }
      } else if (idx == 5U) {
        uint16_t enter_val = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_INVALID_ENTER);
        if (enter_val <= (uint16_t)val) {
          USER_LOG_INFO("[UART]   WARNING: invalid_enter(%u) <= invalid_exit(%lu), ���ͷ����쳣!" USER_LOG_NL,
                        (unsigned)enter_val, val);
        }
      }
    }
    return true;
  }

  /* ---- phone_zone_th_reset: ����������ֵ ---- */
  if (sl_strcasecmp(line, "phone_zone_th_reset") == 0) {
    uint8_t i;
    static const char *const th_names[] = {
      "in_enter", "in_exit", "unlock_enter", "unlock_exit", "invalid_enter", "invalid_exit"
    };

    /* ����ʾ���о�ֵ */
    USER_LOG_INFO("[UART] phone_zone_th_reset: clearing thresholds..." USER_LOG_NL);
    for (i = 0; i < 6; i++) {
      uint16_t old_val = user_app_phone_peps_threshold_get(i);
      USER_LOG_INFO("  [%u] %-14s was %u cm" USER_LOG_NL, (unsigned)i, th_names[i], (unsigned)old_val);
    }

    /* ���� */
    user_app_phone_peps_threshold_reset();

    /* ��ʾĬ��ֵ */
    USER_LOG_INFO("[UART] phone_zone_th_reset: all 6 thresholds reset to defaults (EEPROM cleared):" USER_LOG_NL);
    for (i = 0; i < 6; i++) {
      uint16_t new_val = user_app_phone_peps_threshold_get(i);
      USER_LOG_INFO("  [%u] %-14s = %u cm" USER_LOG_NL, (unsigned)i, th_names[i], (unsigned)new_val);
    }
    return true;
  }

  /* ---- phone_sens_th <1|2|3> <cm>: 设置某档灵敏度的走近解锁距离 ---- */
  if (strncmp(line, "phone_sens_th ", 14) == 0) {
    const char *p = line + 14;
    unsigned long lvl, val;

    /* 解析档位 1-3 (对应协议: 1=近 2=标准 3=远) */
    while (*p == ' ') p++;
    if (*p < '1' || *p > '3') {
      USER_LOG_INFO("[UART] phone_sens_th: invalid level '%c' (need 1-3)" USER_LOG_NL, *p);
      USER_LOG_INFO("[UART]   1=near 2=standard 3=far" USER_LOG_NL);
      return true;
    }
    lvl = (unsigned long)(*p - '0');
    p++;
    while (*p == ' ') p++;

    /* 解析 cm 值 */
    if (*p < '0' || *p > '9') {
      USER_LOG_INFO("[UART] phone_sens_th usage: phone_sens_th <1-3> <cm> (e.g. phone_sens_th 2 700)" USER_LOG_NL);
      return true;
    }
    val = 0;
    while (*p >= '0' && *p <= '9') {
      val = val * 10UL + (unsigned long)(*p - '0');
      if (val > 3000UL) {
        USER_LOG_INFO("[UART] phone_sens_th: value too large (>3000cm)" USER_LOG_NL);
        return true;
      }
      p++;
    }
    if (val < 10UL) {
      USER_LOG_INFO("[UART] phone_sens_th: value too small (<10cm)" USER_LOG_NL);
      return true;
    }

    {
      uint16_t old_val = user_app_phone_peps_sens_dist_get((uint8_t)lvl);
      static const char *const sens_names[] = { "near", "std", "far" };

      user_app_phone_peps_sens_dist_set((uint8_t)lvl, (uint16_t)val);
      USER_LOG_INFO("[UART] phone_sens_th: [%lu] %s %u -> %lu cm (EEPROM written)" USER_LOG_NL,
                    lvl, sens_names[lvl - 1], (unsigned)old_val, val);

      /* 迟滞校验: 不得 >= unlock_exit (同 phone_zone_th 风格, 只告警不拒绝) */
      {
        uint16_t exit_val = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_UNLOCK_EXIT);
        if ((uint16_t)val >= exit_val) {
          USER_LOG_INFO("[UART]   WARNING: sens distance(%lu) >= unlock_exit(%u), hysteresis abnormal!" USER_LOG_NL,
                        val, (unsigned)exit_val);
        }
      }
      /* 提示: 不得 <= in_enter, 否则跳过解锁区直达车内 */
      {
        uint16_t in_enter = user_app_phone_peps_threshold_get(PHONE_PEPS_TH_IDX_IN_ENTER);
        if ((uint16_t)val <= in_enter) {
          USER_LOG_INFO("[UART]   WARNING: sens distance(%lu) <= in_enter(%u), may skip unlock zone!" USER_LOG_NL,
                        val, (unsigned)in_enter);
        }
      }
    }
    return true;
  }

  /* ---- phone_sens_th_reset: 恢复三档默认 ---- */
  if (sl_strcasecmp(line, "phone_sens_th_reset") == 0) {
    int si;
    static const char *const sens_names[] = { "near", "std", "far" };

    USER_LOG_INFO("[UART] phone_sens_th_reset: resetting sens distances..." USER_LOG_NL);
    for (si = 1; si <= 3; si++) {
      uint16_t old_val = user_app_phone_peps_sens_dist_get((uint8_t)si);
      USER_LOG_INFO("  [%d] %-6s was %u cm" USER_LOG_NL, si, sens_names[si - 1], (unsigned)old_val);
    }

    user_app_phone_peps_sens_dist_reset();

    USER_LOG_INFO("[UART] phone_sens_th_reset: sens distances reset to defaults (EEPROM cleared):" USER_LOG_NL);
    for (si = 1; si <= 3; si++) {
      uint16_t new_val = user_app_phone_peps_sens_dist_get((uint8_t)si);
      USER_LOG_INFO("  [%d] %-6s = %u cm" USER_LOG_NL, si, sens_names[si - 1], (unsigned)new_val);
    }
    return true;
  }

  /* ---- phone_lock: ģ�⳵��״̬�仯 ---- */
  if (sl_strcasecmp(line, "phone_lock 0") == 0
      || sl_strcasecmp(line, "phone_lock 1") == 0
      || sl_strcasecmp(line, "phone_lock 2") == 0) {
    /* �ո����ַ����� 0/1/2 */
    const char *p = line + 10;  /* strlen("phone_lock") = 10 */
    while (*p == ' ') p++;
    uint8_t val = (uint8_t)(*p - '0');
    phone_comm_notify_vehicle_lock_state(val);
    USER_LOG_INFO("[UART] phone_lock: set vehicleLockState=%u (%s)" USER_LOG_NL,
                  (unsigned)val,
                  val == 0 ? "UNKNOWN" : val == 1 ? "LOCKED" : "UNLOCKED");
    return true;
  }
  if (strncmp(line, "phone_lock ", 11) == 0) {
    USER_LOG_INFO("[UART] phone_lock usage: phone_lock <0|1|2> (0=UNKNOWN 1=LOCKED 2=UNLOCKED)" USER_LOG_NL);
    return true;
  }

  /* ---- phone_auth_cond: ģ��PEPS/������Ȩ���� ---- */
  if (sl_strcasecmp(line, "phone_auth_cond 0") == 0) {
    phone_comm_set_auth_condition(false);
    USER_LOG_INFO("[UART] phone_auth_cond: set auth_condition=NOT_MET(������)" USER_LOG_NL);
    return true;
  }
  if (sl_strcasecmp(line, "phone_auth_cond 1") == 0) {
    phone_comm_set_auth_condition(true);
    USER_LOG_INFO("[UART] phone_auth_cond: set auth_condition=MET(����)" USER_LOG_NL);
    return true;
  }
  if (strncmp(line, "phone_auth_cond ", 16) == 0) {
    USER_LOG_INFO("[UART] phone_auth_cond usage: phone_auth_cond <0|1> (0=������ 1=����)" USER_LOG_NL);
    return true;
  }

  /* ---- phone_did: ��ʾ�豸 deviceId ---- */
  if (sl_strcasecmp(line, "phone_did") == 0) {
    char did[PHONE_DEVICE_ID_MAX_LEN + 1];
    sl_status_t sc = phone_storage_get_device_id(did, sizeof(did));
    if (sc == SL_STATUS_OK) {
      USER_LOG_INFO("[UART] phone_did: deviceId=%s" USER_LOG_NL, did);
    } else {
      USER_LOG_ERROR("[UART] phone_did: read failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    }
    return true;
  }

  /* ---- hid_cfg <0|1|2>: HID 运行模式 → 立即生效 + 写EE断电保持 ---- */
  if (strncmp(line, "hid_cfg ", 8) == 0) {
    const char *s = line + 8;
    if (s[0] >= '0' && s[0] <= '2' && s[1] == '\0') {
      uint8_t v = (uint8_t)(s[0] - '0');
      const char *desc = (v == 0U) ? "禁用无感" : ((v == 1U) ? "启用(重连,保留Bond)" : "启用(配对,清旧Bond)");
      /* 立即生效 */
      if (v == 0U) {
        hid_service_set_runtime_enabled(false);
      } else {
        if (v == 2U) {
          (void)sl_bt_sm_delete_bondings();   /* 清旧绑定进入可配对 */
        }
        hid_service_set_runtime_enabled(true);
      }
      /* V1.2: refresh adv immediately so HID UUID change is visible on scan */
      phone_comm_switch_to_primary();
      /* 写EE断电保持 (mode = passive_enabled) */
      sl_status_t sc = phone_storage_set_passive_enabled(v != 0U);
      USER_LOG_INFO("[UART] hid_cfg %u: %s 已立即生效并写入EE sc=0x%04lx (断电保持)" USER_LOG_NL,
                    (unsigned)v, desc, (unsigned long)sc);
    } else {
      USER_LOG_INFO("[UART] usage: hid_cfg <0|1|2>  0=禁用无感 1=启用(重连,保留Bond) 2=启用(配对,清旧Bond)  立即生效+写EE  示例: hid_cfg 2" USER_LOG_NL);
    }
    return true;
  }
  if (sl_strcasecmp(line, "hid_cfg") == 0) {
    USER_LOG_INFO("[UART] usage: hid_cfg <0|1|2>  0=禁用无感 1=启用(重连,保留Bond) 2=启用(配对,清旧Bond)  立即生效+写EE  示例: hid_cfg 2" USER_LOG_NL);
    return true;
  }

  /* ---- hid_pin <6位数|0>: 调试固定配对PIN → 立即生效 + 写EE (0=清除) ---- */
  if (strncmp(line, "hid_pin ", 8) == 0) {
    const char *s = line + 8;
    uint32_t val = 0;
    int n = 0;
    bool ok = true;
    while (*s != '\0' && *s != ' ') {
      if (*s < '0' || *s > '9') { ok = false; break; }
      val = val * 10U + (uint32_t)(*s - '0');
      s++;
      n++;
    }
    if (ok && n == 1 && val == 0U) {
      uint32_t zero = 0;
      sl_status_t sc = user_eeprom_write(EEPROM_HID_CFG_PIN, &zero, sizeof(zero), NULL);
      hid_service_set_pin(0);                 /* 清除 → 恢复随机 PIN */
      USER_LOG_INFO("[UART] hid_pin 0: 已清除(下次配对用随机PIN) 写EE sc=0x%04lx" USER_LOG_NL, (unsigned long)sc);
    } else if (ok && n == 6 && val >= 100000U && val <= 999999U) {
      sl_status_t sc = user_eeprom_write(EEPROM_HID_CFG_PIN, &val, sizeof(val), NULL);
      hid_service_set_pin(val);               /* 立即生效 */
      USER_LOG_INFO("[UART] hid_pin %lu: 已立即生效并写入EE sc=0x%04lx (断电保持)" USER_LOG_NL,
                    (unsigned long)val, (unsigned long)sc);
    } else {
      USER_LOG_INFO("[UART] usage: hid_pin <pin>  pin: 6位数字(100000~999999)或0=清除  立即生效+写EE  示例: hid_pin 990011" USER_LOG_NL);
    }
    return true;
  }
  if (sl_strcasecmp(line, "hid_pin") == 0) {
    USER_LOG_INFO("[UART] usage: hid_pin <pin>  pin: 6位数字(100000~999999)或0=清除  立即生效+写EE  示例: hid_pin 990011" USER_LOG_NL);
    return true;
  }

  /* ---- hid_status: 运行状态 + EE 存储配置 ---- */
  if (sl_strcasecmp(line, "hid_status") == 0) {
    bool passive_on = false;
    uint32_t pin = 0;
    (void)phone_storage_get_passive_enabled(&passive_on);
    bool pin_ok = (user_eeprom_read(EEPROM_HID_CFG_PIN, &pin, sizeof(pin)) == SL_STATUS_OK && pin != 0);
    USER_LOG_INFO("[UART] HID 运行状态: runtime=%s connected=%s bonded=%s" USER_LOG_NL,
                  hid_service_is_runtime_enabled() ? "ON" : "OFF",
                  hid_service_is_connected() ? "yes" : "no",
                  hid_service_is_bonded() ? "yes" : "no");
    USER_LOG_INFO("[UART] HID 配置: 无感开关=%s PIN=%s" USER_LOG_NL,
                  passive_on ? "ON" : "OFF",
                  (pin_ok && pin >= 100000UL && pin <= 999999UL) ? "已设置" : "未设置(随机)");
    return true;
  }

  /* ---- hid_adv: enable HID adv + refresh now (for scan), no EE write ---- */
  if (sl_strcasecmp(line, "hid_adv") == 0) {
    hid_service_set_runtime_enabled(true);
    phone_comm_switch_to_primary();
    USER_LOG_INFO("[UART] hid_adv: HID adv enabled + refreshed (scan now)" USER_LOG_NL);
    return true;
  }

  /* ---- reset: ������λ ---- */
  if (sl_strcasecmp(line, "reset") == 0) {
    USER_LOG_INFO("[UART] reset: rebooting MCU..." USER_LOG_NL);
    user_eeprom_flush();
    CHIP_Reset();
    return true;
  }

  /* ---- eeprom_dump: ��ӡ���� EEPROM �洢���� ---- */
  if (sl_strcasecmp(line, "eeprom_dump") == 0) {
    uint16_t i;
    USER_LOG_INFO("[UART] === EEPROM Dump (%u items) ===" USER_LOG_NL,
                  (unsigned)EEPROM_ITEM_COUNT);
    for (i = 0U; i < (uint16_t)EEPROM_ITEM_COUNT; i++) {
      const char *name = (i < (sizeof(g_eeprom_item_names) / sizeof(g_eeprom_item_names[0])))
                         ? g_eeprom_item_names[i] : "?";
      uint32_t nvm_key = user_eeprom_get_nvm_key((user_eeprom_item_t)i);
      uint16_t data_len = user_eeprom_get_data_len((user_eeprom_item_t)i);
      bool valid = user_eeprom_is_valid((user_eeprom_item_t)i);

      USER_LOG_INFO("  [%02u] %-28s nvm=0x%04lX len=%2u valid=%s" USER_LOG_NL,
                    (unsigned)i, name, (unsigned long)nvm_key,
                    (unsigned)data_len, valid ? "YES" : "NO");

      if (valid && data_len > 0U) {
        uint8_t buf[67];
        sl_status_t rsc = user_eeprom_read((user_eeprom_item_t)i, buf, sizeof(buf));
        if (rsc == SL_STATUS_OK) {
          static char hex_buf[256];
          uint16_t pos = 0U;
          uint16_t j;
          for (j = 0U; j < data_len && pos < sizeof(hex_buf) - 4U; j++) {
            pos += (uint16_t)snprintf(&hex_buf[pos], sizeof(hex_buf) - pos,
                                      "%02X ", (unsigned)buf[j]);
          }
          USER_LOG_INFO("       data: %s" USER_LOG_NL, hex_buf);
        }
      }
    }
    return true;
  }

  /* ---- phone_unbind: ����� (������������) ---- */
  if (sl_strcasecmp(line, "phone_unbind") == 0) {
    USER_LOG_INFO("[UART] phone_unbind: erasing binding data (preserving factory data)..." USER_LOG_NL);
    user_eeprom_factory_reset();
    /* V1.2: 解绑同步删除 BLE Bond + 关闭 HID 无感广播, 避免设备端残留旧 Bond
     *       导致下次配对被拒 (0x1205 Pairing Not Supported) */
    (void)sl_bt_sm_delete_bondings();
    hid_service_set_runtime_enabled(false);
    USER_LOG_INFO("[UART] phone_unbind: done. Device is now unbound (bonds cleared)." USER_LOG_NL);
    return true;
  }

  /* ================================================================ */
  /* CAN �������� �� ֱ��д RTE, ������ AppProto_Tx2BE_Handle ����      */
  /* ================================================================ */

  /* can_cmd <0|1|2|3> */
  if (sl_strcasecmp(line, "can_cmd 0") == 0 || sl_strcasecmp(line, "can_cmd 1") == 0
      || sl_strcasecmp(line, "can_cmd 2") == 0 || sl_strcasecmp(line, "can_cmd 3") == 0) {
    const char *p = line + 7; while (*p == ' ') p++;
    uint8_t val = (uint8_t)(*p - '0');
    AppProto_Phone_SetCmd(val);
    USER_LOG_INFO("[CAN_TEST] PhoneCmd �� RTE = %u (%s)" USER_LOG_NL,
                  (unsigned)val,
                  val == 0 ? "NONE" : val == 1 ? "UNLOCK" : val == 2 ? "LOCK" : "FIND");
    return true;
  }
  if (strncmp(line, "can_cmd ", 8) == 0) {
    USER_LOG_INFO("[CAN_TEST] can_cmd usage: can_cmd <0|1|2|3>" USER_LOG_NL);
    return true;
  }

  /* can_lock <0|1|2> */
  if (sl_strcasecmp(line, "can_lock 0") == 0 || sl_strcasecmp(line, "can_lock 1") == 0
      || sl_strcasecmp(line, "can_lock 2") == 0) {
    const char *p = line + 8; while (*p == ' ') p++;
    uint8_t val = (uint8_t)(*p - '0');
    AppProto_Phone_SetLockCmd(val);
    USER_LOG_INFO("[CAN_TEST] PhoneLockCmd �� RTE = %u (%s)" USER_LOG_NL,
                  (unsigned)val,
                  val == 0 ? "NONE" : val == 1 ? "UNLOCK" : "LOCK");
    return true;
  }
  if (strncmp(line, "can_lock ", 9) == 0) {
    USER_LOG_INFO("[CAN_TEST] can_lock usage: can_lock <0|1|2>" USER_LOG_NL);
    return true;
  }

  /* can_pos <0-4> */
  {
    uint8_t val;
    if (sl_strcasecmp(line, "can_pos 0") == 0) { val = 0U;
      AppProto_Phone_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] PhonePos �� RTE = %u (DISCONNECTED_UNKNOWN)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_pos 1") == 0) { val = 1U;
      AppProto_Phone_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] PhonePos �� RTE = %u (IN_CAR)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_pos 2") == 0) { val = 2U;
      AppProto_Phone_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] PhonePos �� RTE = %u (OUTSIDE_UNLOCK)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_pos 3") == 0) { val = 3U;
      AppProto_Phone_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] PhonePos �� RTE = %u (OUTSIDE_LOCK)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_pos 4") == 0) { val = 4U;
      AppProto_Phone_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] PhonePos �� RTE = %u (PARKING_INVALID, CANǯλ��0)" USER_LOG_NL, (unsigned)val); return true; }
  }
  if (strncmp(line, "can_pos ", 8) == 0) {
    USER_LOG_INFO("[CAN_TEST] can_pos usage: can_pos <0|1|2|3|4>" USER_LOG_NL);
    return true;
  }

  /* can_keycmd <0|1|2|3> */
  if (sl_strcasecmp(line, "can_keycmd 0") == 0 || sl_strcasecmp(line, "can_keycmd 1") == 0
      || sl_strcasecmp(line, "can_keycmd 2") == 0 || sl_strcasecmp(line, "can_keycmd 3") == 0) {
    const char *p = line + 10; while (*p == ' ') p++;
    uint8_t val = (uint8_t)(*p - '0');
    AppProto_Key_SetCmd(val);
    USER_LOG_INFO("[CAN_TEST] KeyCmd �� RTE = %u (%s)" USER_LOG_NL,
                  (unsigned)val,
                  val == 0 ? "NONE" : val == 1 ? "UNLOCK" : val == 2 ? "LOCK" : "FIND");
    return true;
  }
  if (strncmp(line, "can_keycmd ", 11) == 0) {
    USER_LOG_INFO("[CAN_TEST] can_keycmd usage: can_keycmd <0|1|2|3>" USER_LOG_NL);
    return true;
  }

  /* can_keylock <0|1|2> */
  if (sl_strcasecmp(line, "can_keylock 0") == 0 || sl_strcasecmp(line, "can_keylock 1") == 0
      || sl_strcasecmp(line, "can_keylock 2") == 0) {
    const char *p = line + 12; while (*p == ' ') p++;
    uint8_t val = (uint8_t)(*p - '0');
    AppProto_Key_SetLockCmd(val);
    USER_LOG_INFO("[CAN_TEST] KeyLockCmd �� RTE = %u (%s)" USER_LOG_NL,
                  (unsigned)val,
                  val == 0 ? "NONE" : val == 1 ? "UNLOCK" : "LOCK");
    return true;
  }
  if (strncmp(line, "can_keylock ", 12) == 0) {
    USER_LOG_INFO("[CAN_TEST] can_keylock usage: can_keylock <0|1|2>" USER_LOG_NL);
    return true;
  }

  /* can_keypos <0-4> */
  {
    uint8_t val;
    if (sl_strcasecmp(line, "can_keypos 0") == 0) { val = 0U;
      AppProto_Key_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] KeyPos �� RTE = %u (DISCONNECTED_UNKNOWN)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_keypos 1") == 0) { val = 1U;
      AppProto_Key_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] KeyPos �� RTE = %u (IN_CAR)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_keypos 2") == 0) { val = 2U;
      AppProto_Key_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] KeyPos �� RTE = %u (OUTSIDE_UNLOCK)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_keypos 3") == 0) { val = 3U;
      AppProto_Key_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] KeyPos �� RTE = %u (OUTSIDE_LOCK)" USER_LOG_NL, (unsigned)val); return true; }
    if (sl_strcasecmp(line, "can_keypos 4") == 0) { val = 4U;
      AppProto_Key_SetPos(val);
      USER_LOG_INFO("[CAN_TEST] KeyPos �� RTE = %u (PARKING_INVALID, CANǯλ��0)" USER_LOG_NL, (unsigned)val); return true; }
  }
  if (strncmp(line, "can_keypos ", 11) == 0) {
    USER_LOG_INFO("[CAN_TEST] can_keypos usage: can_keypos <0|1|2|3|4>" USER_LOG_NL);
    return true;
  }

  /* can_diss <num> */
  if (strncmp(line, "can_diss ", 9) == 0) {
    const char *p = line + 9; while (*p == ' ') p++;
    unsigned long val = 0;
    while (*p >= '0' && *p <= '9') { val = val * 10 + (unsigned long)(*p - '0'); p++; }
    if (p == line + 9 || *p != '\0') {
      USER_LOG_INFO("[CAN_TEST] can_diss usage: can_diss <0-254> (distance in meters, 255=invalid)" USER_LOG_NL);
      return true;
    }
    if (val > 254U) {
      USER_LOG_INFO("[CAN_TEST] can_diss: out of range (0-254)" USER_LOG_NL);
      return true;
    }
    USER_LOG_INFO("[CAN_TEST] can_diss: deprecated (removed from V9 Matrix)" USER_LOG_NL);
    return true;
  }

  /* can_status -- RTE signals only, no raw buffer access */
  if (sl_strcasecmp(line, "can_status") == 0) {
    USER_LOG_INFO("[CAN_TEST] === 0x2BE RTE signals ===" USER_LOG_NL);
    USER_LOG_INFO("  Key:   conn=%u autoEn=%u cmd=%u pos=%u autoLock=%u" USER_LOG_NL,
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Key_Connect),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Key_AutoEn),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Key_Cmd),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Key_Pos),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Key_LockCmd));
    USER_LOG_INFO("  Phone: conn=%u autoEn=%u cmd=%u pos=%u autoLock=%u" USER_LOG_NL,
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Phone_Connect),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Phone_AutoEn),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Phone_Cmd),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Phone_Pos),
                  (unsigned)user_can_rte_read_canSig(RTE_2BE_Phone_LockCmd));
    return true;
  }

  /* ---- can_nm_sleep: 串口强制请求网络休眠 (手动闩锁, 自动判睡期间跳过) ---- */
  if (sl_strcasecmp(line, "can_nm_sleep") == 0) {
    RteSys_SetManualSleepReq(true);
    USER_LOG_INFO("[CAN_TEST] can_nm_sleep: manual sleep latch ON (NM 约4s后停止发送, 收发器进STANDBY)" USER_LOG_NL);
    return true;
  }

  /* ---- can_nm_wake: 取消休眠请求 (软件/本地唤醒; 若自动判睡条件仍满足, 约30s后自动重睡) ---- */
  if (sl_strcasecmp(line, "can_nm_wake") == 0) {
    RteSys_SetManualSleepReq(false);
    RteSys_SetLocalSleepFlag(false);
    USER_LOG_INFO("[CAN_TEST] can_nm_wake: sleep request cleared (本地唤醒)" USER_LOG_NL);
    return true;
  }

  /* ---- can_nm_status: NM/CanManage 状态显示 ---- */
  if (sl_strcasecmp(line, "can_nm_status") == 0) {
    static const char *const nm_state_names[] = {
      "OFF", "BUS_SLEEP", "REPEAT", "NORMAL_OP", "READY_SLEEP", "PREPARE_BUS_SLEEP"
    };
    static const char *const mg_mode_names[] = {
      "INIT", "WAIT", "NORMAL", "TWBS", "SLEEP"
    };
    uint8_t nm_state = (uint8_t)CanNm_GetState();
    uint8_t mg_mode  = (uint8_t)CanManage_GetMode();
    USER_LOG_INFO("[CAN_TEST] === NM Status ===" USER_LOG_NL);
    USER_LOG_INFO("  CanManage mode: %s(%u)" USER_LOG_NL,
                  (mg_mode < 5U) ? mg_mode_names[mg_mode] : "?", (unsigned)mg_mode);
    USER_LOG_INFO("  CanNm  state : %s(%u)" USER_LOG_NL,
                  (nm_state < 6U) ? nm_state_names[nm_state] : "?", (unsigned)nm_state);
    USER_LOG_INFO("  flags: localSleep=%u manual=%u canSleep=%u CAN_REASON=%u" USER_LOG_NL,
                  (unsigned)RteSys_GetLocalSleepFlag(),
                  (unsigned)RteSys_GetManualSleepReq(),
                  (unsigned)RteSys_GetCanSleepFlag(),
                  (unsigned)RteSys_GetBoolSig(RTESYS_BOOL_CAN_REASON_FLAG));
    return true;
  }


  /* ================================================================ */
  /* V1.2 ����״̬�������� (vin / veh)                                  */
  /* ================================================================ */

  /* ---- vin_show: ��ʾ��ǰ VIN ������ VIN ״̬ ---- */
  if (sl_strcasecmp(line, "vin_show") == 0) {
    uint8_t vin[17];
    bool avail = vehicle_state_is_vin_available();
    sl_status_t sc = vehicle_state_get_vin(vin);
    USER_LOG_INFO("[UART] === VIN Status ===" USER_LOG_NL);
    USER_LOG_INFO("  VIU VIN avail: %s" USER_LOG_NL, avail ? "YES" : "NO");
    if (sc == SL_STATUS_OK) {
      USER_LOG_INFO("  VIU VIN:       %.17s" USER_LOG_NL, (const char *)vin);
    } else {
      USER_LOG_INFO("  VIU VIN:       (read failed sc=0x%04lX)" USER_LOG_NL, (unsigned long)sc);
    }
    return true;
  }

  /* ---- vin_set <17chars>: �ֶ�ע�� VIN ---- */
  if (strncmp(line, "vin_set ", 8) == 0) {
    const char *p = line + 8;
    while (*p == ' ') p++;
    if (*p == '\0') {
      USER_LOG_INFO("[UART] vin_set usage: vin_set <17chars> (e.g. vin_set LSVAABBBCCC12345)" USER_LOG_NL);
      return true;
    }
    uint8_t vin[17];
    size_t len = strlen(p);
    memset(vin, 0, 17U);
    memcpy(vin, p, (len < 17U) ? len : 17U);
    vehicle_state_set_vin_override(vin);
    USER_LOG_INFO("[UART] vin_set: VIN override done (%.17s)" USER_LOG_NL, (const char *)vin);
    return true;
  }
  if (strncmp(line, "vin_set", 7) == 0 && (line[7] == '\0' || line[7] == ' ')) {
    if (line[7] == '\0') {
      USER_LOG_INFO("[UART] vin_set usage: vin_set <17chars>" USER_LOG_NL);
    }
    return true;
  }

  /* ---- vin_clear: ����ֶ�ע��� VIN ---- */
  if (sl_strcasecmp(line, "vin_clear") == 0) {
    vehicle_state_clear_vin_override();
    return true;
  }

  /* ---- vin_assoc: ��ʾ VIN ����״̬ ---- */
  if (sl_strcasecmp(line, "vin_assoc") == 0) {
    USER_LOG_INFO("[UART] === VIN Association ===" USER_LOG_NL);
    USER_LOG_INFO("  state:      %s (%u)" USER_LOG_NL,
                  user_vin_state_name(user_vin_get_state()),
                  (unsigned)user_vin_get_state());
    USER_LOG_INFO("  is_learned: %s" USER_LOG_NL, user_vin_is_learned() ? "YES" : "NO");
    USER_LOG_INFO("  is_match:   %s" USER_LOG_NL, user_vin_is_match() ? "YES" : "NO");
    USER_LOG_INFO("  abstract:   %s (%u)" USER_LOG_NL,
                  (user_vin_get_abstract_status() == 0) ? "NORMAL" :
                  (user_vin_get_abstract_status() == 1) ? "ABNORMAL" : "UNCONFIRMED",
                  (unsigned)user_vin_get_abstract_status());
    return true;
  }

  /* ---- veh_state: ��ʾ��������״̬ ---- */
  if (sl_strcasecmp(line, "veh_state") == 0) {
    uint8_t vin[17];
    bool vin_avail;
    sl_status_t vin_sc;

    vin_avail = vehicle_state_is_vin_available();
    vin_sc = vehicle_state_get_vin(vin);

    USER_LOG_INFO("[UART] === Vehicle State ===" USER_LOG_NL);
    USER_LOG_INFO("  override:  %s" USER_LOG_NL, vehicle_state_is_override_active() ? "YES" : "NO (CAN)");
    USER_LOG_INFO("  VIN:       %s (%.17s)" USER_LOG_NL,
                  vin_avail ? "valid" : "N/A",
                  (vin_sc == SL_STATUS_OK) ? (const char *)vin : "");
    USER_LOG_INFO("  ignition:  %u (%s)" USER_LOG_NL,
                  (unsigned)vehicle_state_get_ignition_gear(),
                  (vehicle_state_get_ignition_gear() == 0) ? "OFF" :
                  (vehicle_state_get_ignition_gear() == 1) ? "ACC" :
                  (vehicle_state_get_ignition_gear() == 2) ? "ON" : "START");
    USER_LOG_INFO("  range:     %u km" USER_LOG_NL, (unsigned)vehicle_state_get_remaining_range_km());
    {
      uint8_t doors = vehicle_state_get_door_status();
      USER_LOG_INFO("  doors:     0x%02X (FL=%s FR=%s RL=%s RR=%s)" USER_LOG_NL,
                    (unsigned)doors,
                    (doors & 0x01) ? "OPEN" : "CLOSED",
                    (doors & 0x02) ? "OPEN" : "CLOSED",
                    (doors & 0x04) ? "OPEN" : "CLOSED",
                    (doors & 0x08) ? "OPEN" : "CLOSED");
    }
    {
      uint8_t lock = vehicle_state_get_final_lock_state();
      USER_LOG_INFO("  lock:      %u (%s)" USER_LOG_NL, (unsigned)lock,
                    (lock == 1) ? "LOCKED" : (lock == 2) ? "UNLOCKED" : "UNKNOWN");
    }
    USER_LOG_INFO("  PEPS key:  %s" USER_LOG_NL,
                  vehicle_state_is_peps_key_in_car() ? "IN_CAR" : "NOT_IN_CAR");
    return true;
  }

  /* ---- veh_set <ign> <range> <doors> <lock> <peps>: �ֶ�ע�복��״̬ ---- */
  if (strncmp(line, "veh_set ", 8) == 0) {
    const char *p = line + 8;
    unsigned long vals[5];
    int parsed = 0;
    memset(vals, 0, sizeof(vals));
    while (*p == ' ') p++;
    {
      int i;
      for (i = 0; i < 5; i++) {
        if (*p < '0' || *p > '9') break;
        unsigned long v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (unsigned long)(*p - '0'); p++; }
        vals[i] = v;
        parsed++;
        while (*p == ' ') p++;
      }
    }
    if (parsed < 5) {
      USER_LOG_INFO("[UART] veh_set usage: veh_set <ign> <range> <doors> <lock> <peps>" USER_LOG_NL);
      USER_LOG_INFO("[UART]   ign=0-3  range=0-65535  doors=0x00-0x0F  lock=0-2  peps=0-1" USER_LOG_NL);
      USER_LOG_INFO("[UART]   e.g. veh_set 2 350 0 1 1" USER_LOG_NL);
      return true;
    }
    if (vals[0] > 3U)  { USER_LOG_INFO("[UART] veh_set: invalid ign (%lu, 0-3)" USER_LOG_NL, vals[0]); return true; }
    if (vals[1] > 65535U) { USER_LOG_INFO("[UART] veh_set: invalid range (%lu)" USER_LOG_NL, vals[1]); return true; }
    if (vals[2] > 15U) { USER_LOG_INFO("[UART] veh_set: invalid doors (%lu, 0x00-0x0F)" USER_LOG_NL, vals[2]); return true; }
    if (vals[3] > 2U)  { USER_LOG_INFO("[UART] veh_set: invalid lock (%lu, 0-2)" USER_LOG_NL, vals[3]); return true; }
    if (vals[4] > 1U)  { USER_LOG_INFO("[UART] veh_set: invalid peps (%lu, 0-1)" USER_LOG_NL, vals[4]); return true; }
    vehicle_state_set_override((uint8_t)vals[0], (uint16_t)vals[1],
                               (uint8_t)vals[2], (uint8_t)vals[3], (bool)vals[4]);
    return true;
  }
  if (strncmp(line, "veh_set", 7) == 0 && (line[7] == '\0' || line[7] == ' ')) {
    if (line[7] == '\0') {
      USER_LOG_INFO("[UART] veh_set usage: veh_set <ign> <range> <doors> <lock> <peps>" USER_LOG_NL);
    }
    return true;
  }

  /* ---- veh_clear: ����ֶ�ע�� ---- */
  if (sl_strcasecmp(line, "veh_clear") == 0) {
    vehicle_state_clear_override();
    return true;
  }

  /* ---- veh_override: �鿴�Ƿ�ʹ�� override ���� ---- */
  if (sl_strcasecmp(line, "veh_override") == 0) {
    USER_LOG_INFO("[UART] veh_override: %s" USER_LOG_NL,
                  vehicle_state_is_override_active() ? "YES (using manual injection)" : "NO (using CAN/RTE source)");
    return true;
  }

  return false;
}