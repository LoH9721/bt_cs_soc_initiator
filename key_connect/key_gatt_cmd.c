/***************************************************************************//**
 * @file key_gatt_cmd.c
 * @brief 控制器侧钥匙通信协议实现 (详细日志版)
 ******************************************************************************/
#include "key_gatt_cmd.h"
#include "key_gatt_ecdsa.h"
#include "key_gatt_comm.h"
#include "key_connect.h"
#include "app.h"
#include "user_log_console.h"
#include "sl_sleeptimer.h"
#include "user_eeprom/user_eeprom.h"
#include <string.h>

// ===== 常量 =====
#define PHASE_A_RETRY_MAX       3
#define PHASE_B_RETRY_MAX       3
#define STEP_TIMEOUT_MS         2000
// ===== 钥匙域 NVM Key 已迁移到 user_eeprom 统一管理 =====
#define DIR_INIT_TO_REFL  0x00
#define DIR_REFL_TO_INIT  0x01

// 带时间戳的日志宏 [ms] 前缀，便于定位时序
#define PROTO_LOG(fmt, ...) \
  USER_LOG_INFO("[%08lu]" fmt, (unsigned long)proto_now_ms(), ##__VA_ARGS__)
#define PROTO_ERR(fmt, ...) \
  USER_LOG_ERROR("[%08lu]" fmt, (unsigned long)proto_now_ms(), ##__VA_ARGS__)

static const char *phase_name(proto_phase_t p) {
  switch (p) {
    case PROTO_PHASE_IDLE:        return "IDLE";
    case PROTO_PHASE_A:           return "PHASE_A";
    case PROTO_PHASE_B_INIT:      return "PHASE_B_INIT";
    case PROTO_PHASE_B_CONFIRM:   return "PHASE_B_CONFIRM";
    case PROTO_PHASE_C:           return "PHASE_C";
    default: return "???";
  }
}
static const char *cmd_name(uint8_t c) {
  switch (c) {
    case 0x10: return "0x10 PUBKEY_REQ";
    case 0x11: return "0x11 PUBKEY_RSP";
    case 0x20: return "0x20 SESSION_INIT";
    case 0x21: return "0x21 SESSION_RSP";
    case 0x22: return "0x22 SESSION_CONFIRM_C";
    case 0x23: return "0x23 SESSION_CONFIRM_K";
    case 0x30: return "0x30 BUTTON_EVENT";
    case 0x31: return "0x31 STATUS_REPORT";
    case 0x32: return "0x32 ACK";
    case 0xFF: return "0xFF ERROR";
    default: return "???";
  }
}

// ===== CCM Nonce =====
static void build_ccm_nonce(uint32_t seq, uint8_t dir, uint8_t nonce[13])
{
  nonce[0] = (seq >> 0)  & 0xFF;
  nonce[1] = (seq >> 8)  & 0xFF;
  nonce[2] = (seq >> 16) & 0xFF;
  nonce[3] = (seq >> 24) & 0xFF;
  nonce[4] = dir;
  memset(nonce + 5, 0, 8);
}

static uint64_t proto_now_ms(void) {
  return sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count64());
}

// ===== 内部状态 =====
static proto_phase_t proto_phase = PROTO_PHASE_IDLE;
static bool         is_pairing_session;
static uint8_t      session_key[16];
static uint32_t     seq_init_local;
static uint32_t     seq_c;
static uint32_t     last_received_seq;
static uint64_t     step_deadline_ms;
static uint8_t      retry_count;
static uint8_t      local_tx_retry;       // 本地快速重试 (100ms)，应对 BLE 栈瞬时忙
static uint8_t      peer_pubkey_ram[64];
static bool         peer_pubkey_valid;
static uint8_t      key1[16], key2[16];
static bool         key1_confirmed;
static uint8_t      last_ack_frame[32];
static uint8_t      last_ack_len;
static uint32_t     last_ack_seq;
static proto_button_cb_t button_cb = NULL;
static proto_status_cb_t status_cb = NULL;
static bool         conn_closed_flag = false;
static int          idle_last_state = -1;  // 断连重置，确保重连 IDLE 检查日志可见

// ===== hex 打印辅助 =====
static void log_hex(const char *tag, const uint8_t *d, int n) {
  PROTO_LOG("[PROTO] %s: ", tag);
  for (int i = 0; i < n; i++) USER_LOG_INFO("%02X", d[i]);
  USER_LOG_INFO("" USER_LOG_NL);
}
static void log_pubkey_first(const char *tag, const uint8_t pk[64]) {
  PROTO_LOG("[PROTO] %s: x[0..3]=%02X%02X%02X%02X y[0..3]=%02X%02X%02X%02X" USER_LOG_NL,
    tag, pk[0],pk[1],pk[2],pk[3], pk[32],pk[33],pk[34],pk[35]);
}

// ===== 辅助 =====
static void proto_delete_bondings(void) {
  sl_bt_sm_delete_bondings();
  PROTO_LOG("[PROTO] >>> bonds DELETED" USER_LOG_NL);
}
static void proto_disconnect(void) {
  uint8_t c = app_key_conn_handle;
  PROTO_LOG("[PROTO] >>> disconnect (conn=%u)" USER_LOG_NL, c);
  if (c != SL_BT_INVALID_CONNECTION_HANDLE) sl_bt_connection_close(c);
}
static void proto_clear_ram_secrets(void) {
  PROTO_LOG("[PROTO] >>> clear RAM secrets (session_key, key1, key2, peer_pubkey)" USER_LOG_NL);
  key_crypto_memzero(session_key, 16);
  key_crypto_memzero(key1, 16);
  key_crypto_memzero(key2, 16);
  key_crypto_memzero(peer_pubkey_ram, 64);
  key1_confirmed = false;
  peer_pubkey_valid = false;
}

// ===== NVM (基于 user_eeprom 统一存储层 / CRC16 自动校验) =====
static bool nvm_read_peer_pubkey(uint8_t buf[64]) {
  sl_status_t sc = user_eeprom_read(EEPROM_KEY_PEER_PUBKEY, buf, 64U);
  PROTO_LOG("[PROTO] NVM read 0x4000 sc=0x%04lx" USER_LOG_NL, (unsigned long)sc);
  return (sc == SL_STATUS_OK);
}
static bool nvm_write_peer_pubkey(const uint8_t buf[64]) {
  sl_status_t sc = user_eeprom_write(EEPROM_KEY_PEER_PUBKEY, buf, 64U, NULL);
  PROTO_LOG("[PROTO] NVM write 0x4000 (64B) sc=0x%04lx" USER_LOG_NL, (unsigned long)sc);
  log_pubkey_first("NVM write pubkey", buf);
  return (sc == SL_STATUS_OK);
}
static uint8_t nvm_get_pairing_state(void) {
  uint8_t v = 0xFF;
  sl_status_t sc = user_eeprom_read(EEPROM_KEY_PAIRING_STATE, &v, 1U);
  if (sc != SL_STATUS_OK) { PROTO_LOG("[PROTO] NVM read 0x4002: NOT FOUND -> 0x00" USER_LOG_NL); return 0x00; }
  if (v == 0x00 || v == 0x01) { PROTO_LOG("[PROTO] NVM read 0x4002: 0x%02X" USER_LOG_NL, v); return v; }
  PROTO_LOG("[PROTO] NVM read 0x4002: invalid=0x%02X -> 0x00" USER_LOG_NL, v);
  return 0x00;
}
static bool nvm_set_pairing_state(uint8_t v) {
  sl_status_t sc = user_eeprom_write(EEPROM_KEY_PAIRING_STATE, &v, 1U, NULL);
  PROTO_LOG("[PROTO] NVM write 0x4002=0x%02X sc=0x%04lx" USER_LOG_NL, v, (unsigned long)sc);
  return (sc == SL_STATUS_OK);
}
static void nvm_clear_proto_nvm(void) {
  PROTO_LOG("[PROTO] NVM delete 0x4000 + 0x4002" USER_LOG_NL);
  user_eeprom_delete(EEPROM_KEY_PEER_PUBKEY);
  user_eeprom_delete(EEPROM_KEY_PAIRING_STATE);
}
static bool peer_pubkey_nvm_valid(const uint8_t pk[64]) {
  /* user_eeprom 在 init() 时已做 CRC16 校验, 此处仅做 RAM 层非零检查 */
  uint8_t o = 0, a = 0xFF;
  for (int i = 0; i < 64; i++) { o |= pk[i]; a &= pk[i]; }
  bool v = (o != 0x00) && (a != 0xFF);
  PROTO_LOG("[PROTO] NVM pubkey check: or=0x%02X and=0x%02X valid=%d" USER_LOG_NL, o, a, v);
  return v;
}

// ===== 帧发送 =====
static bool proto_send_frame(uint8_t cmd, const uint8_t *payload, uint8_t len) {
  uint8_t conn = key_gatt_comm_get_connection();
  if (conn == SL_BT_INVALID_CONNECTION_HANDLE) {
    PROTO_ERR("[PROTO] TX FAIL: no connection" USER_LOG_NL);
    return false;
  }
  uint8_t buf[200];
  buf[0] = cmd; buf[1] = len;
  if (len > 0 && payload) memcpy(buf + 2, payload, len);
  PROTO_LOG("[PROTO] --- TX %s len=%u conn=%u ---" USER_LOG_NL, cmd_name(cmd), 2+len, conn);
  sl_status_t sc = key_gatt_comm_send(conn, buf, 2 + len);
  if (sc != SL_STATUS_OK) PROTO_ERR("[PROTO] TX FAIL: sc=0x%04lx" USER_LOG_NL, (unsigned long)sc);
  return (sc == SL_STATUS_OK);
}
static void proto_send_error(uint8_t err) {
  PROTO_LOG("[PROTO] >>> send ERROR 0x%02X" USER_LOG_NL, err);
  proto_send_frame(PROTO_CMD_ERROR, &err, 1);
}

// ===== Cleanup (§8.3) =====
static void proto_cleanup_on_error(uint8_t err, proto_phase_t phase) {
  PROTO_LOG("[PROTO] ======== CLEANUP START ========" USER_LOG_NL);
  PROTO_LOG("[PROTO]   err=0x%02X phase=%s is_pairing=%d" USER_LOG_NL, err, phase_name(phase), is_pairing_session);
  bool ph_a_no_nvm = (phase == PROTO_PHASE_A);
  bool ph_b_nvm    = (phase == PROTO_PHASE_B_INIT || phase == PROTO_PHASE_B_CONFIRM);

  if (err == PROTO_ERR_PHASEA_REJECT || err == PROTO_ERR_NEED_PHASEA) {
    PROTO_LOG("[PROTO]   action: delete_bonds + clear_NVM + disconnect (NVM asymmetry)" USER_LOG_NL);
    proto_delete_bondings(); nvm_clear_proto_nvm(); proto_disconnect();
  } else if (err == PROTO_ERR_DECRYPT_FAIL || err == PROTO_ERR_SEQ_FAULT) {
    PROTO_LOG("[PROTO]   action: disconnect_only (Phase C recoverable)" USER_LOG_NL);
    proto_disconnect();
  } else if (err == PROTO_ERR_TIMEOUT) {
    if (is_pairing_session && ph_a_no_nvm) {
      PROTO_LOG("[PROTO]   action: delete_bonds + disconnect (Phase A, NVM not committed)" USER_LOG_NL);
      proto_delete_bondings(); proto_disconnect();
    } else if (is_pairing_session && ph_b_nvm) {
      PROTO_LOG("[PROTO]   action: delete_bonds + clear_NVM + disconnect (Phase B, NVM committed)" USER_LOG_NL);
      proto_delete_bondings(); nvm_clear_proto_nvm(); proto_disconnect();
    } else {
      PROTO_LOG("[PROTO]   action: disconnect_only (reconnect or Phase C)" USER_LOG_NL);
      proto_disconnect();
    }
  } else if (is_pairing_session) {
    if (ph_a_no_nvm) {
      PROTO_LOG("[PROTO]   action: delete_bonds + disconnect (Phase A)" USER_LOG_NL);
      proto_delete_bondings(); proto_disconnect();
    } else {
      PROTO_LOG("[PROTO]   action: delete_bonds + clear_NVM + disconnect (Phase B)" USER_LOG_NL);
      proto_delete_bondings(); nvm_clear_proto_nvm(); proto_disconnect();
    }
  } else {
    PROTO_LOG("[PROTO]   action: disconnect_only (reconnect)" USER_LOG_NL);
    proto_disconnect();
  }
  proto_clear_ram_secrets();
  memset(last_ack_frame, 0, sizeof(last_ack_frame));
  last_ack_len = 0; last_ack_seq = 0;
  proto_phase = PROTO_PHASE_IDLE;     // 防止 process_action 重复触发超时
  step_deadline_ms = 0; retry_count = 0;
  PROTO_LOG("[PROTO] ======== CLEANUP DONE (-> IDLE) ========" USER_LOG_NL);
}

// ===== 前向声明 =====
static void phase_b_start(void);

// ==================== Phase A 公钥交换 ====================

static void phase_a_send_0x10(void) {
  uint8_t pubkey[64];
  if (!key_crypto_get_local_pubkey(pubkey)) {
    PROTO_ERR("[PROTO] Phase A: get_local_pubkey FAIL" USER_LOG_NL);
    proto_cleanup_on_error(PROTO_ERR_TIMEOUT, PROTO_PHASE_A);
    return;
  }
  log_pubkey_first("0x10 pubkey_c", pubkey);
  proto_send_frame(PROTO_CMD_PUBKEY_REQ, pubkey, 64);
}

static void phase_a_start(void) {
  PROTO_LOG("[PROTO] ====== PHASE A START (公钥交换) ======" USER_LOG_NL);
  proto_phase = PROTO_PHASE_A;
  retry_count = 0;
  phase_a_send_0x10();
  step_deadline_ms = proto_now_ms() + STEP_TIMEOUT_MS;
  PROTO_LOG("[PROTO]   deadline=%lu (+2s)" USER_LOG_NL, (unsigned long)step_deadline_ms);
}

static void phase_a_handle_rx(uint8_t cmd, const uint8_t *payload, uint8_t len) {
  if (cmd != PROTO_CMD_PUBKEY_RSP || len < 65) {
    PROTO_LOG("[PROTO] Phase A RX: unexpected cmd=%s len=%u, ignored" USER_LOG_NL, cmd_name(cmd), len);
    return;
  }
  uint8_t key_ver = payload[64];
  PROTO_LOG("[PROTO] <<< 0x11 RX key_ver=%u" USER_LOG_NL, key_ver);
  log_pubkey_first("0x11 pubkey_k", payload);

  if (key_ver < PROTO_MIN_SUPPORTED_VER) {
    PROTO_ERR("[PROTO] Phase A: KEY_VER REJECT %u < %u" USER_LOG_NL, key_ver, PROTO_MIN_SUPPORTED_VER);
    proto_send_error(PROTO_ERR_VER_MISMATCH);
    proto_cleanup_on_error(PROTO_ERR_VER_MISMATCH, PROTO_PHASE_A);
    return;
  }
  memcpy(peer_pubkey_ram, payload, 64);
  peer_pubkey_valid = true;
  PROTO_LOG("[PROTO] >>> Phase A OK -> 进入 Phase B" USER_LOG_NL);
  proto_phase = PROTO_PHASE_B_INIT;
  retry_count = 0;
  phase_b_start();
}

static void phase_a_process_timeout(void) {
  uint64_t now = proto_now_ms();
  if (now < step_deadline_ms) return;
  uint64_t elapsed = now - (step_deadline_ms - STEP_TIMEOUT_MS);
  PROTO_LOG("[PROTO] Phase A: timeout elapsed=%lums retry=%d/%d" USER_LOG_NL,
                (unsigned long)elapsed, retry_count, PHASE_A_RETRY_MAX);
  if (retry_count < PHASE_A_RETRY_MAX) {
    retry_count++;
    phase_a_send_0x10();
    step_deadline_ms = now + STEP_TIMEOUT_MS;
  } else {
    PROTO_ERR("[PROTO] Phase A: ALL RETRIES EXHAUSTED" USER_LOG_NL);
    proto_send_error(PROTO_ERR_TIMEOUT);
    proto_cleanup_on_error(PROTO_ERR_TIMEOUT, PROTO_PHASE_A);
  }
}

// ==================== Phase B 安全通道建立 ====================

static void phase_b_start(void) {
  PROTO_LOG("[PROTO] ====== PHASE B START (安全通道建立) ======" USER_LOG_NL);
  key_crypto_get_random(key1, 16);
  log_hex("key1", key1, 16);

  // 签名
  uint8_t hash[32];
  const char *str = "SESS-INIT";
  uint8_t sign_input[25];
  memcpy(sign_input, key1, 16);
  memcpy(sign_input + 16, str, 9);
  key_crypto_sha256(sign_input, 25, hash);
  log_hex("ECDSA sign hash(key1||SESS-INIT)", hash, 32);

  uint8_t sig_c[64];
  key_crypto_ecdsa_sign_by_id(0, hash, sig_c);
  log_hex("sig_c[0..15]", sig_c, 16);

  uint8_t payload[80];
  memcpy(payload, key1, 16);
  memcpy(payload + 16, sig_c, 64);

  // NVM commit
  if (is_pairing_session && peer_pubkey_valid) {
    PROTO_LOG("[PROTO] >>> PAIRING: commit NVM (pubkey_k + 0x4002=0x01)" USER_LOG_NL);
    nvm_write_peer_pubkey(peer_pubkey_ram);
    nvm_set_pairing_state(0x01);
  } else {
    PROTO_LOG("[PROTO] >>> RECONNECT: skip NVM commit (is_pairing=%d)" USER_LOG_NL, is_pairing_session);
  }
  key1_confirmed = false;
  bool sent = proto_send_frame(PROTO_CMD_SESSION_INIT, payload, 80);
  if (!sent) {
    PROTO_LOG("[PROTO]   0x20 send FAIL, start local quick-retry (100ms x2)" USER_LOG_NL);
    local_tx_retry = 2;
    step_deadline_ms = proto_now_ms() + 100; // 100ms 快速重试
  } else {
    local_tx_retry = 0;
    step_deadline_ms = proto_now_ms() + STEP_TIMEOUT_MS;
    PROTO_LOG("[PROTO]   deadline=%lu" USER_LOG_NL, (unsigned long)step_deadline_ms);
  }
}

static void phase_b_handle_0x21(const uint8_t *payload, uint8_t len) {
  if (len < 83) {
    PROTO_ERR("[PROTO] Phase B: 0x21 len=%u < 83, ignored" USER_LOG_NL, len);
    return;
  }
  const uint8_t *recv_key2 = payload;
  const uint8_t *sig_k = payload + 16;
  uint8_t key_ver = payload[80];
  uint16_t bat = (uint16_t)payload[81] | ((uint16_t)payload[82] << 8);

  PROTO_LOG("[PROTO] <<< 0x21 RX key_ver=%u bat=%umV" USER_LOG_NL, key_ver, bat);
  log_hex("recv key2", recv_key2, 16);
  log_hex("recv sig_k[0..15]", sig_k, 16);

  if (key_ver < PROTO_MIN_SUPPORTED_VER) {
    PROTO_ERR("[PROTO] Phase B: key_ver=%u < MIN=%u REJECT" USER_LOG_NL, key_ver, PROTO_MIN_SUPPORTED_VER);
    proto_send_error(PROTO_ERR_VER_MISMATCH);
    proto_cleanup_on_error(PROTO_ERR_VER_MISMATCH, PROTO_PHASE_B_INIT);
    return;
  }

  // 验签
  uint8_t hash[32];
  const char *str = "SESS-RESP";
  uint8_t verify_input[41];
  memcpy(verify_input, recv_key2, 16);
  memcpy(verify_input + 16, key1, 16);
  memcpy(verify_input + 32, str, 9);
  key_crypto_sha256(verify_input, 41, hash);
  log_hex("ECDSA verify hash(key2||key1||SESS-RESP)", hash, 32);

  const uint8_t *pubkey = NULL;
  uint8_t nvm_pk[64];
  if (peer_pubkey_valid) {
    pubkey = peer_pubkey_ram;
    PROTO_LOG("[PROTO]   using pubkey from RAM" USER_LOG_NL);
  } else if (nvm_read_peer_pubkey(nvm_pk)) {
    pubkey = nvm_pk;
    PROTO_LOG("[PROTO]   using pubkey from NVM" USER_LOG_NL);
  }
  if (pubkey == NULL) {
    PROTO_ERR("[PROTO] Phase B: NO PEER PUBKEY AVAILABLE" USER_LOG_NL);
    proto_send_error(PROTO_ERR_SIG_FAIL);
    proto_cleanup_on_error(PROTO_ERR_SIG_FAIL, PROTO_PHASE_B_INIT);
    return;
  }
  log_pubkey_first("verify pubkey", pubkey);

  bool verify_ok = key_crypto_ecdsa_verify(pubkey, hash, sig_k);
  PROTO_LOG("[PROTO]   ECDSA verify: %s" USER_LOG_NL, verify_ok ? "PASS" : "FAIL");
  if (!verify_ok) {
    proto_send_error(PROTO_ERR_SIG_FAIL);
    proto_cleanup_on_error(PROTO_ERR_SIG_FAIL, PROTO_PHASE_B_INIT);
    return;
  }

  memcpy(key2, recv_key2, 16);
  key1_confirmed = true;

  key_crypto_derive_session_key(key1, key2, session_key);
  log_hex("session_key", session_key, 16);

  // 生成 seq_init_c
  int seq_attempts = 0;
  do {
    key_crypto_get_random((uint8_t *)&seq_init_local, 4);
    seq_attempts++;
  } while ((seq_init_local == 0x00000000 || seq_init_local == 0xFFFFFFFE || seq_init_local == 0xFFFFFFFF) && seq_attempts < 100);
  PROTO_LOG("[PROTO]   seq_c_init=0x%08lX (attempts=%d)" USER_LOG_NL, (unsigned long)seq_init_local, seq_attempts);

  // 构建 0x22 (AAD=2B)
  uint8_t ccm_plain[14];
  memcpy(ccm_plain, "SESSION-OK", 10);
  ccm_plain[10] = (seq_init_local >> 0)  & 0xFF;
  ccm_plain[11] = (seq_init_local >> 8)  & 0xFF;
  ccm_plain[12] = (seq_init_local >> 16) & 0xFF;
  ccm_plain[13] = (seq_init_local >> 24) & 0xFF;

  uint8_t aad[2] = { PROTO_CMD_SESSION_CONFIRM_C, 18 };
  uint8_t nonce[13]; build_ccm_nonce(0x00000000, DIR_INIT_TO_REFL, nonce);
  log_hex("0x22 nonce[0..5]", nonce, 6);
  PROTO_LOG("[PROTO]   0x22 AAD len=2: %02X %02X" USER_LOG_NL, aad[0], aad[1]);

  uint8_t ct[18], mic[4];
  bool enc_ok = key_crypto_aes_ccm_encrypt(session_key, nonce, ccm_plain, 14, aad, 2, ct, mic);
  PROTO_LOG("[PROTO]   0x22 CCM encrypt: %s" USER_LOG_NL, enc_ok ? "OK" : "FAIL");
  log_hex("0x22 ccm_plain", ccm_plain, 14);
  log_hex("0x22 MIC", mic, 4);

  uint8_t frame[20];
  frame[0] = PROTO_CMD_SESSION_CONFIRM_C; frame[1] = 18;
  memcpy(frame + 2, ct, 14); memcpy(frame + 16, mic, 4);

  proto_phase = PROTO_PHASE_B_CONFIRM;
  seq_c = seq_init_local;
  last_received_seq = 0xFFFFFFFF;
  retry_count = 0;
  sl_status_t sc = key_gatt_comm_send(key_gatt_comm_get_connection(), frame, 20);
  if (sc != SL_STATUS_OK) {
    PROTO_LOG("[PROTO]   0x22 send FAIL sc=0x%04lx, start local quick-retry (100ms x2)" USER_LOG_NL, (unsigned long)sc);
    local_tx_retry = 2;
    step_deadline_ms = proto_now_ms() + 100;
  } else {
    local_tx_retry = 0;
    step_deadline_ms = proto_now_ms() + STEP_TIMEOUT_MS;
    PROTO_LOG("[PROTO]   -> PHASE_B_CONFIRM, waiting 0x23..." USER_LOG_NL);
  }
}

static void phase_b_handle_0x23(const uint8_t *payload, uint8_t len) {
  if (len < 18) {
    PROTO_ERR("[PROTO] Phase B: 0x23 len=%u < 18" USER_LOG_NL, len);
    return;
  }
  PROTO_LOG("[PROTO] <<< 0x23 RX (len=%u)" USER_LOG_NL, len);
  uint8_t aad[2] = { PROTO_CMD_SESSION_CONFIRM_K, 18 };
  uint8_t nonce[13]; build_ccm_nonce(0x00000000, DIR_REFL_TO_INIT, nonce);
  log_hex("0x23 nonce[0..5]", nonce, 6);
  PROTO_LOG("[PROTO]   0x23 AAD len=2: %02X %02X" USER_LOG_NL, aad[0], aad[1]);

  const uint8_t *ct = payload, *mic = payload + 14;
  log_hex("0x23 recv ct[0..13]", ct, 14);
  log_hex("0x23 recv MIC", mic, 4);
  (void)len;

  uint8_t ccm_plain[14];
  bool dec_ok = key_crypto_aes_ccm_decrypt(session_key, nonce, ct, 14, aad, 2, mic, ccm_plain);
  PROTO_LOG("[PROTO]   0x23 CCM decrypt: %s" USER_LOG_NL, dec_ok ? "OK" : "FAIL");
  if (!dec_ok) {
    proto_send_error(PROTO_ERR_SESSION_FAIL);
    proto_cleanup_on_error(PROTO_ERR_SESSION_FAIL, PROTO_PHASE_B_CONFIRM);
    return;
  }
  log_hex("0x23 ccm_plain", ccm_plain, 14);

  if (memcmp(ccm_plain, "SESSION-OK", 10) != 0) {
    PROTO_ERR("[PROTO] Phase B: 0x23 plaintext != SESSION-OK" USER_LOG_NL);
    proto_send_error(PROTO_ERR_SESSION_FAIL);
    proto_cleanup_on_error(PROTO_ERR_SESSION_FAIL, PROTO_PHASE_B_CONFIRM);
    return;
  }

  uint32_t seq_k_init = (uint32_t)ccm_plain[10] | ((uint32_t)ccm_plain[11] << 8)
                      | ((uint32_t)ccm_plain[12] << 16) | ((uint32_t)ccm_plain[13] << 24);
  PROTO_LOG("[PROTO]   seq_k_init=0x%08lX" USER_LOG_NL, (unsigned long)seq_k_init);

  bool seq_ok = (seq_k_init != 0x00000000 && seq_k_init != 0xFFFFFFFE && seq_k_init != 0xFFFFFFFF);
  PROTO_LOG("[PROTO]   seq_k_init validation: %s" USER_LOG_NL, seq_ok ? "OK" : "INVALID");
  if (!seq_ok) {
    proto_send_error(PROTO_ERR_SIG_FAIL);
    proto_cleanup_on_error(PROTO_ERR_SIG_FAIL, PROTO_PHASE_B_CONFIRM);
    return;
  }

  last_received_seq = seq_k_init - 1;
  proto_phase = PROTO_PHASE_C;
  PROTO_LOG("[PROTO]   last_received_seq = 0x%08lX" USER_LOG_NL, (unsigned long)last_received_seq);
  PROTO_LOG("[PROTO] ====== PHASE B DONE -> PHASE C (加密通信) ======" USER_LOG_NL);
}

static void phase_b_process_timeout(void) {
  uint64_t now = proto_now_ms();
  if (now < step_deadline_ms) return;

  // 本地快速重试 (100ms x N): BLE 栈瞬时忙时快速恢复
  if (local_tx_retry > 0) {
    local_tx_retry--;
    bool sent = false;
    if (proto_phase == PROTO_PHASE_B_INIT) {
      PROTO_LOG("[PROTO]   0x20 local quick-retry (%d left)" USER_LOG_NL, local_tx_retry);
      uint8_t hash[32]; uint8_t sig_c[64];
      const char *str = "SESS-INIT"; uint8_t sign_input[25];
      memcpy(sign_input, key1, 16); memcpy(sign_input + 16, str, 9);
      key_crypto_sha256(sign_input, 25, hash);
      key_crypto_ecdsa_sign_by_id(0, hash, sig_c);
      uint8_t payload[80]; memcpy(payload, key1, 16); memcpy(payload + 16, sig_c, 64);
      sent = proto_send_frame(PROTO_CMD_SESSION_INIT, payload, 80);
    } else {
      PROTO_LOG("[PROTO]   0x22 local quick-retry (%d left)" USER_LOG_NL, local_tx_retry);
      uint8_t ccm_plain[14]; memcpy(ccm_plain, "SESSION-OK", 10);
      ccm_plain[10] = (seq_init_local >> 0)  & 0xFF;
      ccm_plain[11] = (seq_init_local >> 8)  & 0xFF;
      ccm_plain[12] = (seq_init_local >> 16) & 0xFF;
      ccm_plain[13] = (seq_init_local >> 24) & 0xFF;
      uint8_t aad[2] = { PROTO_CMD_SESSION_CONFIRM_C, 18 };
      uint8_t nonce[13]; build_ccm_nonce(0x00000000, DIR_INIT_TO_REFL, nonce);
      uint8_t ct[18], mic[4];
      key_crypto_aes_ccm_encrypt(session_key, nonce, ccm_plain, 14, aad, 2, ct, mic);
      uint8_t frame[20]; frame[0] = PROTO_CMD_SESSION_CONFIRM_C; frame[1] = 18;
      memcpy(frame + 2, ct, 14); memcpy(frame + 16, mic, 4);
      sent = key_gatt_comm_send(key_gatt_comm_get_connection(), frame, 20);
    }
    if (sent) {
      PROTO_LOG("[PROTO]   quick-retry OK, resume normal timeout" USER_LOG_NL);
      local_tx_retry = 0;
      retry_count = 0;
      step_deadline_ms = now + STEP_TIMEOUT_MS;
    } else if (local_tx_retry > 0) {
      step_deadline_ms = now + 100;
    } else {
      PROTO_LOG("[PROTO]   local quick-retry exhausted, enter normal retry" USER_LOG_NL);
      retry_count = 0;
      step_deadline_ms = now + STEP_TIMEOUT_MS;
    }
    return;
  }

  uint64_t elapsed = now - (step_deadline_ms - STEP_TIMEOUT_MS);
  PROTO_LOG("[PROTO] Phase B: timeout %s elapsed=%lums retry=%d/%d" USER_LOG_NL,
                phase_name(proto_phase), (unsigned long)elapsed, retry_count, PHASE_B_RETRY_MAX);

  if (retry_count < PHASE_B_RETRY_MAX) {
    retry_count++;
    step_deadline_ms = now + STEP_TIMEOUT_MS;
    if (proto_phase == PROTO_PHASE_B_INIT) {
      PROTO_LOG("[PROTO]   0x20 retry (same key1)" USER_LOG_NL);
      uint8_t hash[32]; uint8_t sig_c[64];
      const char *str = "SESS-INIT"; uint8_t sign_input[25];
      memcpy(sign_input, key1, 16); memcpy(sign_input + 16, str, 9);
      key_crypto_sha256(sign_input, 25, hash);
      key_crypto_ecdsa_sign_by_id(0, hash, sig_c);
      uint8_t payload[80]; memcpy(payload, key1, 16); memcpy(payload + 16, sig_c, 64);
      proto_send_frame(PROTO_CMD_SESSION_INIT, payload, 80);
    } else {
      PROTO_LOG("[PROTO]   0x22 retry (same seq_c_init)" USER_LOG_NL);
      uint8_t ccm_plain[14]; memcpy(ccm_plain, "SESSION-OK", 10);
      ccm_plain[10] = (seq_init_local >> 0)  & 0xFF;
      ccm_plain[11] = (seq_init_local >> 8)  & 0xFF;
      ccm_plain[12] = (seq_init_local >> 16) & 0xFF;
      ccm_plain[13] = (seq_init_local >> 24) & 0xFF;
      uint8_t aad[2] = { PROTO_CMD_SESSION_CONFIRM_C, 18 };
      uint8_t nonce[13]; build_ccm_nonce(0x00000000, DIR_INIT_TO_REFL, nonce);
      uint8_t ct[18], mic[4];
      key_crypto_aes_ccm_encrypt(session_key, nonce, ccm_plain, 14, aad, 2, ct, mic);
      uint8_t frame[20]; frame[0] = PROTO_CMD_SESSION_CONFIRM_C; frame[1] = 18;
      memcpy(frame + 2, ct, 14); memcpy(frame + 16, mic, 4);
      key_gatt_comm_send(key_gatt_comm_get_connection(), frame, 20);
    }
  } else {
    PROTO_ERR("[PROTO] Phase B: ALL RETRIES EXHAUSTED" USER_LOG_NL);
    proto_send_error(PROTO_ERR_TIMEOUT);
    proto_cleanup_on_error(PROTO_ERR_TIMEOUT, proto_phase);
  }
}

// ==================== Phase C 命令交互 ====================

static void phase_c_send_ack(uint32_t ack_seq, uint8_t result) {
  uint8_t ack_plain[5];
  ack_plain[0] = (ack_seq >> 0)  & 0xFF;
  ack_plain[1] = (ack_seq >> 8)  & 0xFF;
  ack_plain[2] = (ack_seq >> 16) & 0xFF;
  ack_plain[3] = (ack_seq >> 24) & 0xFF;
  ack_plain[4] = result;

  seq_c++;
  // Phase C operational log suppressed

  uint8_t aad[6]; aad[0] = PROTO_CMD_ACK; aad[1] = 13; memcpy(aad + 2, &seq_c, 4);
  uint8_t nonce[13]; build_ccm_nonce(seq_c, DIR_INIT_TO_REFL, nonce);
  uint8_t ct[13], mic[4];
  key_crypto_aes_ccm_encrypt(session_key, nonce, ack_plain, 5, aad, 6, ct, mic);

  // 缓存
  last_ack_frame[0] = PROTO_CMD_ACK; last_ack_frame[1] = 13;
  memcpy(last_ack_frame + 2, &seq_c, 4);
  memcpy(last_ack_frame + 6, ct, 5);
  memcpy(last_ack_frame + 11, mic, 4);
  last_ack_len = 15; last_ack_seq = ack_seq; // CMD(1)+LEN(1)+seq(4)+ct(5)+mic(4)

  key_gatt_comm_send(key_gatt_comm_get_connection(), last_ack_frame, last_ack_len);
  // Phase C operational log suppressed
}

static void phase_c_handle_encrypted_frame(
    uint8_t cmd, const uint8_t *frame, uint8_t total_len,
    uint8_t exp_ct_len, proto_button_cb_t btn_h, proto_status_cb_t stat_h)
{
  uint8_t exp_total = 2 + 4 + exp_ct_len + 4;
  if (total_len < exp_total) {
    PROTO_ERR("[PROTO] Phase C frame too short %u < %u" USER_LOG_NL, total_len, exp_total);
    return;
  }
  const uint8_t *p = frame + 2;
  uint32_t seq = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
  const uint8_t *ct = p + 4, *mic = ct + exp_ct_len;

  // seq 检查
  if (seq <= last_received_seq) {
    if (seq == last_received_seq && last_ack_len > 0) {
      key_gatt_comm_send(key_gatt_comm_get_connection(), last_ack_frame, last_ack_len);
      return;
    }
    PROTO_ERR("[PROTO] Phase C seq regression seq=0x%08lX last=0x%08lX" USER_LOG_NL,
              (unsigned long)seq, (unsigned long)last_received_seq);
    proto_send_error(PROTO_ERR_SEQ_FAULT);
    proto_cleanup_on_error(PROTO_ERR_SEQ_FAULT, PROTO_PHASE_C);
    return;
  }

  // AES-CCM 解密 (AAD=6B)
  uint8_t aad[6]; aad[0] = cmd; aad[1] = 4 + exp_ct_len + 4; memcpy(aad + 2, p, 4);
  uint8_t nonce[13]; build_ccm_nonce(seq, DIR_REFL_TO_INIT, nonce);

  uint8_t pt[8];
  if (!key_crypto_aes_ccm_decrypt(session_key, nonce, ct, exp_ct_len, aad, 6, mic, pt)) {
    PROTO_ERR("[PROTO] Phase C CCM decrypt FAIL cmd=%s" USER_LOG_NL, cmd_name(cmd));
    proto_send_error(PROTO_ERR_DECRYPT_FAIL);
    proto_cleanup_on_error(PROTO_ERR_DECRYPT_FAIL, PROTO_PHASE_C);
    return;
  }

  last_received_seq = seq;
  phase_c_send_ack(seq, 0x00);
 
  // 业务回调
  if (cmd == PROTO_CMD_BUTTON_EVENT && btn_h) { 
    uint8_t b1=pt[0], b2=pt[1], b3=pt[2];
    uint16_t b=(uint16_t)pt[3]|((uint16_t)pt[4]<<8);
    btn_h(b1, b2, b3, b, pt[5]);
  } else if (cmd == PROTO_CMD_STATUS_REPORT && stat_h) {
    uint16_t b=(uint16_t)pt[0]|((uint16_t)pt[1]<<8);
    stat_h(b, (int8_t)pt[2], pt[3]);
  }
}

// ===== Error =====
static void handle_error_frame(const uint8_t *payload, uint8_t len) {
  if (len < 1) return;
  uint8_t err = payload[0];
  PROTO_LOG("[PROTO] <<< 0xFF ERROR code=0x%02X" USER_LOG_NL, err);
  proto_cleanup_on_error(err, proto_phase);
}

// ===== RX Dispatch =====
static void proto_on_rx(uint8_t conn, const uint8_t *data, uint8_t len) {
  (void)conn;
  if (data == NULL || len < 2) return;
  uint8_t cmd = data[0], payload_len = data[1];
  if (2 + payload_len > len) {
    PROTO_ERR("[PROTO] RX: bad frame cmd=%s payload_len=%u > available=%u" USER_LOG_NL,
                   cmd_name(cmd), payload_len, len - 2);
    return;
  }
  const uint8_t *payload = data + 2;
  if (proto_phase != PROTO_PHASE_C) // Phase C 周期流量不打印 dispatch
    PROTO_LOG("[PROTO] >>> RX dispatch: cmd=%s len=%u phase=%s" USER_LOG_NL,
                  cmd_name(cmd), payload_len, phase_name(proto_phase));

  if (cmd == PROTO_CMD_ERROR) { handle_error_frame(payload, payload_len); return; }

  switch (proto_phase) {
    case PROTO_PHASE_A:
      if (cmd == PROTO_CMD_PUBKEY_RSP) phase_a_handle_rx(cmd, payload, payload_len);
      else PROTO_LOG("[PROTO]   ignored in PHASE_A" USER_LOG_NL);
      break;
    case PROTO_PHASE_B_INIT:
      if (cmd == PROTO_CMD_SESSION_RSP) phase_b_handle_0x21(payload, payload_len);
      else PROTO_LOG("[PROTO]   ignored in PHASE_B_INIT" USER_LOG_NL);
      break;
    case PROTO_PHASE_B_CONFIRM:
      if (cmd == PROTO_CMD_SESSION_CONFIRM_K) phase_b_handle_0x23(payload, payload_len);
      else if (cmd == PROTO_CMD_SESSION_RSP) {
        PROTO_LOG("[PROTO]   cross-step: dup 0x21 -> re-handle" USER_LOG_NL);
        phase_b_handle_0x21(payload, payload_len);
      } else PROTO_LOG("[PROTO]   ignored in PHASE_B_CONFIRM" USER_LOG_NL);
      break;
    case PROTO_PHASE_C:
      if (cmd == PROTO_CMD_BUTTON_EVENT) 
      {
        PROTO_LOG("[PROTO]   encrypted frame RX cmd=%s len=%u" USER_LOG_NL, cmd_name(cmd), payload_len);
        phase_c_handle_encrypted_frame(cmd, data, len, 8, button_cb, NULL);
      } 
      else if (cmd == PROTO_CMD_STATUS_REPORT) phase_c_handle_encrypted_frame(cmd, data, len, 8, NULL, status_cb);
      else PROTO_LOG("[PROTO]   cmd=%s ignored in PHASE_C" USER_LOG_NL, cmd_name(cmd));
      break;
    default: break;
  }
}

// ===== Public API =====
void key_gatt_proto_init(void) {
  PROTO_LOG("[PROTO] ====== init start ======" USER_LOG_NL);
  key_crypto_init();
  key_gatt_comm_set_rx_callback(proto_on_rx);
  proto_phase = PROTO_PHASE_IDLE; is_pairing_session = false;
  proto_clear_ram_secrets();
  memset(last_ack_frame, 0, sizeof(last_ack_frame));
  last_ack_len = 0; last_ack_seq = 0;
  seq_c = 0; last_received_seq = 0xFFFFFFFF;
  conn_closed_flag = false;
  PROTO_LOG("[PROTO] ====== init done ======" USER_LOG_NL);
}

void key_gatt_proto_process_action(void) {
  if (conn_closed_flag) {
    conn_closed_flag = false;
    PROTO_LOG("[PROTO] ====== BLE DISCONNECTED (phase=%s) ======" USER_LOG_NL, phase_name(proto_phase));
    if (proto_phase != PROTO_PHASE_IDLE) {
      proto_clear_ram_secrets();
      memset(last_ack_frame, 0, sizeof(last_ack_frame));
      last_ack_len = 0; is_pairing_session = false;
    }
    proto_phase = PROTO_PHASE_IDLE;
    PROTO_LOG("[PROTO] -> IDLE" USER_LOG_NL);
    return;
  }

  if (proto_phase == PROTO_PHASE_IDLE) {
    bool conn = key_connect_is_connected();
    bool bonded = key_connect_is_bonded();
    bool gatt_ready = key_gatt_comm_is_ready();
    int cur_state = (conn?4:0) | (bonded?2:0) | (gatt_ready?1:0);
    if (cur_state != idle_last_state) {
      PROTO_LOG("[PROTO] IDLE check: conn=%d bonded=%d gatt_ready=%d" USER_LOG_NL, conn, bonded, gatt_ready);
      idle_last_state = cur_state;
    }
    if (!conn || !bonded || !gatt_ready) return;
    idle_last_state = -1; // 条件满足进入 Phase，下次重新检测

    uint8_t pairing_state = nvm_get_pairing_state();
    is_pairing_session = (pairing_state == 0x00);
    PROTO_LOG("[PROTO] >>> CONDITIONS MET -> is_pairing_session=%d (0x4002=0x%02X)" USER_LOG_NL,
                  is_pairing_session, pairing_state);

    if (is_pairing_session) {
      phase_a_start();
    } else {
      uint8_t nvm_pk[64];
      if (nvm_read_peer_pubkey(nvm_pk) && peer_pubkey_nvm_valid(nvm_pk)) {
        memcpy(peer_pubkey_ram, nvm_pk, 64); peer_pubkey_valid = true;
        proto_phase = PROTO_PHASE_B_INIT;
        PROTO_LOG("[PROTO] RECONNECT: skip Phase A, go Phase B" USER_LOG_NL);
        phase_b_start();
      } else {
        PROTO_LOG("[PROTO] NVM pubkey invalid -> clear NVM + fallback Phase A" USER_LOG_NL);
        nvm_clear_proto_nvm(); is_pairing_session = true;
        phase_a_start();
      }
    }
    return;
  }

  if (proto_phase == PROTO_PHASE_A) phase_a_process_timeout();
  else if (proto_phase == PROTO_PHASE_B_INIT || proto_phase == PROTO_PHASE_B_CONFIRM)
    phase_b_process_timeout();
}

void key_gatt_proto_on_bt_event(sl_bt_msg_t *evt) {
  if (evt == NULL) return;
  if (SL_BT_MSG_ID(evt->header) == sl_bt_evt_connection_closed_id) {
    // 不比对 handle: PM 事件可能在 BT 事件前清掉 app_key_conn_handle
    PROTO_LOG("[PROTO] BLE event: connection_closed conn=%u" USER_LOG_NL,
              evt->data.evt_connection_closed.connection);
    conn_closed_flag = true;
  }
}
bool key_gatt_proto_is_in_phase_c(void) { return (proto_phase == PROTO_PHASE_C); }
void key_gatt_proto_set_button_callback(proto_button_cb_t cb) { button_cb = cb; }
void key_gatt_proto_set_status_callback(proto_status_cb_t cb) { status_cb = cb; }

void key_gatt_proto_reset_pairing(void) {
  PROTO_LOG("[PROTO] >>> reset_pairing: clear NVM + reset to IDLE" USER_LOG_NL);
  nvm_clear_proto_nvm();
  proto_clear_ram_secrets();
  proto_phase = PROTO_PHASE_IDLE;
  is_pairing_session = false;
  peer_pubkey_valid = false;
}
