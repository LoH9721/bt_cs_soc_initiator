/***************************************************************************//**
 * @file phone_session.c
 * @brief V1.1 会话管理实现：全部 RAM 状态 + 超时处理。
 ******************************************************************************/

#include "user_phone/data/phone_session.h"
#include "user_phone/data/phone_crypto.h"
#include "user_phone/data/phone_storage.h"
#include "user_phone/phone_cfg.h"
#include "user_log_console.h"
#include "sl_sleeptimer.h"
#include "sl_bt_api.h"
#include <string.h>

/* ========================================================================== */
/* 毫秒时间                                                                    */
/* ========================================================================== */

/* ARM 32-bit newlib-nano printf 有 %ll 格式 bug, 用纯手动格式化避开 */
static const char *u64_dec_str(uint64_t val)
{
  static char buf[2][21];
  static int idx = 0;
  char *b = buf[(idx++) & 1];
  int pos = 20;
  b[pos--] = '\0';
  if (val == 0ULL) {
    b[pos--] = '0';
  } else {
    while (val > 0ULL) {
      b[pos--] = (char)('0' + (unsigned)(val % 10ULL));
      val /= 10ULL;
    }
  }
  return &b[pos + 1];
}

uint64_t phone_session_now_ms(void)
{
  uint64_t freq = sl_sleeptimer_get_timer_frequency();
  uint64_t tick = sl_sleeptimer_get_tick_count64();
  return (tick * 1000ULL) / freq;
}

/* ========================================================================== */
/* 生命周期                                                                    */
/* ========================================================================== */

void phone_session_init(phone_session_t *sess)
{
  if (sess == NULL) return;
  memset(sess, 0, sizeof(*sess));

  /* 从 NVM 加载持久化状态 */
  sess->device_state = phone_storage_get_bind_state();
  sess->conn_handle  = 0xFFU;  /* 无效句柄 */
  sess->seq_tx       = 1U;     /* Seq 从 1 开始 */

  /* V1.2: 从 NVM 恢复 passive 状态 */
  {
    bool pe;
    (void)phone_storage_get_passive_enabled(&pe);
    sess->passive_enabled = pe;
    sess->hid_runtime_enabled = false;  /* 重启后 HID runtime 必须 OFF */
    uint8_t sens;
    (void)phone_storage_get_passive_sensitivity(&sens);
    sess->passive_sensitivity = sens;
    /* V1.2: quota 持久化, 上电从 NVM 恢复 (未写入则 0, 需 APP 刷新) */
    {
      uint32_t quota;
      (void)phone_storage_get_passive_quota(&quota);
      sess->passive_quota_remaining = quota;
      USER_LOG_INFO("[SESSION] passive_quota loaded from NVM = %u" USER_LOG_NL,
                    (unsigned)quota);
    }
  }
}

void phone_session_reset(phone_session_t *sess)
{
  if (sess == NULL) return;

  /* V1.2: 断连前恢复 SM 默认配置 (避免残留 displayonly 模式) */
  if (sess->pairing_window_active && !sess->pairing_awaiting_system) {
    (void)sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_SC_ONLY
                             | SL_BT_SM_CONFIGURATION_BONDING_REQUIRED,
                             sl_bt_sm_io_capability_noinputnooutput);
  }

  /* 销毁所有 RAM 密钥 */
  phone_crypto_memzero(sess->bind_session_key, 16U);
  phone_crypto_memzero(sess->bg_ecdh_privkey, 32U);
  phone_crypto_memzero(sess->session_key, 16U);

  /* 保留: device_state, silent_until_ms, is_silent,
   *       pairing_fail_count_this_session (与 AUTH session 绑定),
   *       passive_enabled, passive_sensitivity (持久化字段)
   *       (断连后 SILENT 继续计时) */
  bool was_silent = sess->is_silent;
  uint64_t silent_until = sess->silent_until_ms;
  uint8_t pairing_fails = sess->pairing_fail_count_this_session;
  bool was_passive_enabled = sess->passive_enabled;
  uint8_t was_passive_sensitivity = sess->passive_sensitivity;
  uint32_t was_passive_quota = sess->passive_quota_remaining;
  /* V1.2: 等待系统 Pairing 时, 配对上下文跨断连保持 (passkey/windowId/deadline) */
  bool was_pairing_awaiting = sess->pairing_awaiting_system;
  uint32_t was_pairing_passkey = sess->pairing_passkey;
  uint64_t was_pairing_deadline = sess->pairing_window_deadline_ms;
  uint8_t was_pairing_window_id[8];
  if (was_pairing_awaiting) {
    memcpy(was_pairing_window_id, sess->pairing_window_id, 8U);
  }

  memset(sess, 0, sizeof(*sess));

  sess->device_state   = phone_storage_get_bind_state();
  sess->conn_handle    = 0xFFU;
  sess->seq_tx         = 1U;
  sess->is_silent      = was_silent;
  sess->silent_until_ms = silent_until;
  /* V1.2: 候选 VIN 断连即失效 */
  sess->candidate_vin_valid = false;
  /* V1.2: 配对上下文默认断连即失效, 但失败计数保留 (与 AUTH session 绑定);
   *        等待系统 Pairing 时跨断连保持 */
  sess->pairing_window_active = false;
  sess->pairing_fail_count_this_session = pairing_fails;
  if (was_pairing_awaiting) {
    sess->pairing_awaiting_system = true;
    sess->pairing_window_active = true;
    sess->pairing_passkey = was_pairing_passkey;
    sess->pairing_window_deadline_ms = was_pairing_deadline;
    memcpy(sess->pairing_window_id, was_pairing_window_id, 8U);
  }
  /* V1.2: passive 持久化字段断连保留 (quota 现在也持久化), transient 字段清零 */
  sess->passive_enabled = was_passive_enabled;
  sess->passive_sensitivity = was_passive_sensitivity;
  sess->passive_quota_remaining = was_passive_quota;
  sess->hid_runtime_enabled = false;
}

/* ========================================================================== */
/* 绑定会话                                                                    */
/* ========================================================================== */

void phone_session_begin_bind(phone_session_t *sess,
                              const uint8_t app_nonce[16],
                              const uint8_t bg_nonce[16],
                              const uint8_t session_id[16],
                              const uint8_t session_key[16],
                              uint16_t remain_sec)
{
  if (sess == NULL) return;

  memcpy(sess->app_bind_nonce,  app_nonce,  16U);
  memcpy(sess->bg_bind_nonce,   bg_nonce,   16U);
  memcpy(sess->bind_session_id, session_id, 16U);
  memcpy(sess->bind_session_key,session_key,16U);

  sess->bind_deadline_ms   = phone_session_now_ms() + (uint64_t)remain_sec * 1000ULL;
  sess->bind_session_active = true;
  sess->qr_verified         = false;
  sess->authorization_met   = false;
  sess->candidate_vin_valid = false;

  /* 新绑定会话重置计数器, 避免继承上次连接的残留值 */
  sess->security_counter_rx = 0ULL;
  sess->security_counter_tx = 0ULL;
}

bool phone_session_is_bind_active(const phone_session_t *sess)
{
  if (sess == NULL) return false;
  if (!sess->bind_session_active) return false;
  /* 检查超时 (由 process_timeouts 周期性更新) */
  return sess->bind_session_active;
}

void phone_session_set_qr_verified(phone_session_t *sess)
{
  if (sess != NULL) sess->qr_verified = true;
}

void phone_session_set_authorized(phone_session_t *sess)
{
  if (sess != NULL) sess->authorization_met = true;
}

void phone_session_end_bind(phone_session_t *sess)
{
  if (sess == NULL) return;
  phone_crypto_memzero(sess->bind_session_key, 16U);
  phone_crypto_memzero(sess->candidate_vin, 17U);
  sess->bind_session_active  = false;
  sess->qr_verified          = false;
  sess->authorization_met    = false;
  sess->candidate_vin_valid  = false;
}

/* ========================================================================== */
/* 已绑定认证会话                                                              */
/* ========================================================================== */

sl_status_t phone_session_begin_auth(phone_session_t *sess,
                                     const uint8_t app_key_id[16])
{
  sl_status_t sc;

  if (sess == NULL) return SL_STATUS_INVALID_PARAMETER;
  (void)app_key_id;  /* 已通过 appKeyId 校验 */

  /* 生成 BG24 临时 ECDH 密钥对 */
  sc = phone_crypto_ecdh_generate(sess->bg_ecdh_pubkey, sess->bg_ecdh_privkey);
  if (sc != SL_STATUS_OK) return sc;

  /* appEcdhPublicKey 和 nonceA 由 APP 在 AUTH_CHALLENGE_RSP 中传入, 此处不保存 */

  /* 生成随机 nonceB / authSessionId / challengeId */
  sc = phone_crypto_get_random(sess->nonce_b, 16U);
  if (sc != SL_STATUS_OK) { phone_crypto_memzero(sess->bg_ecdh_privkey, 32U); return sc; }

  sc = phone_crypto_get_random(sess->auth_session_id, 16U);
  if (sc != SL_STATUS_OK) { phone_crypto_memzero(sess->bg_ecdh_privkey, 32U); return sc; }

  sc = phone_crypto_get_random(sess->challenge_id, 16U);
  if (sc != SL_STATUS_OK) { phone_crypto_memzero(sess->bg_ecdh_privkey, 32U); return sc; }

  sess->auth_deadline_ms      = phone_session_now_ms() + PHONE_TIMEOUT_AUTH_CHALLENGE_MS;
  sess->auth_challenge_active  = true;
  sess->auth_done             = false;

  /* V1.2: 新 AUTH session → 清零配对失败计数 */
  sess->pairing_fail_count_this_session = 0U;

  return SL_STATUS_OK;
}

bool phone_session_is_auth_challenge_active(const phone_session_t *sess)
{
  if (sess == NULL) return false;
  return sess->auth_challenge_active && !sess->auth_done;
}

sl_status_t phone_session_complete_auth(phone_session_t *sess,
                                        const uint8_t session_key[16])
{
  if (sess == NULL) return SL_STATUS_INVALID_PARAMETER;

  memcpy(sess->session_key, session_key, 16U);
  sess->auth_done             = true;
  sess->auth_challenge_active  = false;
  sess->security_counter_rx   = 0ULL;
  sess->security_counter_tx   = 0ULL;  /* 下一个发送为 1 */

  return SL_STATUS_OK;
}

bool phone_session_is_authenticated(const phone_session_t *sess)
{
  if (sess == NULL) return false;
  return sess->auth_done;
}

/* ========================================================================== */
/* 控制 challenge                                                              */
/* ========================================================================== */

sl_status_t phone_session_begin_ctrl_challenge(phone_session_t *sess)
{
  sl_status_t sc;

  if (sess == NULL) return SL_STATUS_INVALID_PARAMETER;

  sc = phone_crypto_get_random(sess->ctrl_challenge_id, 16U);
  if (sc != SL_STATUS_OK) return sc;

  sc = phone_crypto_get_random(sess->ctrl_nonce, 16U);
  if (sc != SL_STATUS_OK) return sc;

  sess->ctrl_deadline_ms      = phone_session_now_ms() + PHONE_TIMEOUT_CTRL_CHALLENGE_MS;
  sess->ctrl_challenge_active  = true;

  return SL_STATUS_OK;
}

bool phone_session_consume_ctrl_challenge(phone_session_t *sess)
{
  if (sess == NULL) return false;
  if (!sess->ctrl_challenge_active) return false;

  sess->ctrl_challenge_active = false;
  phone_crypto_memzero(sess->ctrl_nonce, 16U);
  return true;
}

bool phone_session_is_ctrl_challenge_active(const phone_session_t *sess)
{
  if (sess == NULL) return false;
  return sess->ctrl_challenge_active;
}

/* ========================================================================== */
/* 计数器                                                                      */
/* ========================================================================== */

uint64_t phone_session_next_tx_counter(phone_session_t *sess)
{
  if (sess == NULL) return 0ULL;
  sess->security_counter_tx++;
  return sess->security_counter_tx;
}

uint16_t phone_session_next_tx_seq(phone_session_t *sess)
{
  uint16_t seq;
  if (sess == NULL) return 0U;

  seq = sess->seq_tx;
  sess->seq_tx++;
  if (sess->seq_tx == 0U) {
    sess->seq_tx = 1U;  /* 跳过 0x0000 (Event 专用) */
  }
  return seq;
}

sl_status_t phone_session_check_rx_counter(phone_session_t *sess, uint64_t counter)
{
  if (sess == NULL) return SL_STATUS_INVALID_PARAMETER;

  /* 首帧: counter 必须为 1 */
  if (sess->security_counter_rx == 0ULL) {
    if (counter != 1ULL) {
      return SL_STATUS_INVALID_PARAMETER;
    }
    return SL_STATUS_OK;
  }

  /* 正常新帧: counter == last_accepted + 1 */
  if (counter != sess->security_counter_rx + 1ULL) {
    /* 跳号 → 安全错误, APP 应断开重连 */
    return SL_STATUS_INVALID_PARAMETER;
  }

  return SL_STATUS_OK;
}

void phone_session_accept_rx_counter(phone_session_t *sess, uint64_t counter)
{
  if (sess == NULL) return;
  sess->security_counter_rx = counter;
  sess->seq_rx = 0U;  /* seq_rx 由上层单独追踪 */
}

/* ========================================================================== */
/* 幂等缓存                                                                    */
/* ========================================================================== */

bool phone_session_check_idempotent(phone_session_t *sess, uint8_t cmd, uint16_t seq,
                                    uint8_t *response_buf, uint16_t *response_len)
{
  if (sess == NULL || sess->idem_response_len == 0U) return false;

  /* 检查缓存有效性 */
  if (phone_session_now_ms() > sess->idem_deadline_ms) {
    sess->idem_response_len = 0U;
    return false;
  }

  /* 匹配 Cmd+Seq */
  if (sess->idem_cmd != cmd || sess->idem_seq != seq) {
    return false;
  }

  if (response_buf != NULL && response_len != NULL) {
    memcpy(response_buf, sess->idem_response, sess->idem_response_len);
    *response_len = sess->idem_response_len;
  }
  return true;
}

void phone_session_cache_idempotent(phone_session_t *sess, uint8_t cmd, uint16_t seq,
                                    const uint8_t *response, uint16_t response_len)
{
  if (sess == NULL || response == NULL || response_len == 0U) return;
  if (response_len > sizeof(sess->idem_response)) return;

  sess->idem_cmd  = cmd;
  sess->idem_seq  = seq;
  memcpy(sess->idem_response, response, response_len);
  sess->idem_response_len = response_len;
  sess->idem_deadline_ms  = phone_session_now_ms() + PHONE_TIMEOUT_IDEMPOTENT_CACHE_MS;
}

/* ========================================================================== */
/* 安全失败 / SILENT                                                           */
/* ========================================================================== */

void phone_session_record_security_failure(phone_session_t *sess)
{
  if (sess == NULL) return;

  sess->security_fail_count++;

  if (sess->security_fail_count >= PHONE_SILENT_MAX_FAIL_COUNT) {
    sess->is_silent = true;
    sess->silent_until_ms = phone_session_now_ms()
                          + (uint64_t)PHONE_SILENT_DEFAULT_SEC * 1000ULL;
    sess->device_state = PHONE_DEVICE_STATE_SILENT;
    (void)phone_storage_set_bind_state(PHONE_DEVICE_STATE_SILENT);
    /* SILENT 期间禁止控制操作, 主动作废已有 challenge 并清零 nonce */
    sess->ctrl_challenge_active = false;
    phone_crypto_memzero(sess->ctrl_nonce, 16U);
    USER_LOG_INFO("[SESSION] SILENT_ENTER fail#=%u until_ms=%s (ctrl_challenge cleared)" USER_LOG_NL,
                  (unsigned)sess->security_fail_count,
                  u64_dec_str(sess->silent_until_ms));
  }
}

void phone_session_clear_silent(phone_session_t *sess)
{
  if (sess == NULL) return;
  sess->is_silent           = false;
  sess->silent_until_ms     = 0ULL;
  sess->security_fail_count = 0U;
  /* 恢复实际的 bindState */
  sess->device_state = phone_storage_get_bind_state();
}

void phone_session_clear_security_failures(phone_session_t *sess)
{
  if (sess != NULL) {
    sess->security_fail_count = 0U;
  }
}

/* ========================================================================== */
/* 超时处理                                                                    */
/* ========================================================================== */

void phone_session_process_timeouts(phone_session_t *sess)
{
  uint64_t now;

  if (sess == NULL) return;
  now = phone_session_now_ms();

  /* 绑定会话超时 */
  if (sess->bind_session_active && sess->bind_deadline_ms > 0ULL) {
    if (now > sess->bind_deadline_ms) {
      /* 换绑窗口超时: 回退到 BOUND, 保留旧绑定 */
      if (sess->device_state == PHONE_DEVICE_STATE_REBIND_WINDOW) {
        sess->device_state = PHONE_DEVICE_STATE_BOUND;
        (void)phone_storage_set_bind_state(PHONE_DEVICE_STATE_BOUND);
        USER_LOG_INFO("[SESSION] REBIND_TIMEOUT → BOUND" USER_LOG_NL);
      }
      phone_session_end_bind(sess);
    }
  }

  /* AUTH challenge 超时 */
  if (sess->auth_challenge_active && sess->auth_deadline_ms > 0ULL) {
    if (now > sess->auth_deadline_ms) {
      phone_crypto_memzero(sess->bg_ecdh_privkey, 32U);
      sess->auth_challenge_active = false;
    }
  }

  /* 控制 challenge 超时 */
  if (sess->ctrl_challenge_active && sess->ctrl_deadline_ms > 0ULL) {
    if (now > sess->ctrl_deadline_ms) {
      sess->ctrl_challenge_active = false;
      phone_crypto_memzero(sess->ctrl_nonce, 16U);
    }
  }

  /* 幂等缓存超时 */
  if (sess->idem_response_len > 0U && sess->idem_deadline_ms > 0ULL) {
    if (now > sess->idem_deadline_ms) {
      sess->idem_response_len = 0U;
    }
  }

  /* SILENT 超时恢复 */
  if (sess->is_silent && sess->silent_until_ms > 0ULL) {
    if (now > sess->silent_until_ms) {
      sess->is_silent       = false;
      sess->silent_until_ms = 0ULL;
      sess->security_fail_count = 0U;
      sess->device_state = phone_storage_get_bind_state();
      (void)phone_storage_set_bind_state(sess->device_state);
      USER_LOG_INFO("[SESSION] SILENT_EXIT → state=0x%02X" USER_LOG_NL,
                    (unsigned)sess->device_state);
    }
  }

  /* V1.2: PASSIVE_PAIR 窗口超时 (30s PREPARE 或 60s READY) */
  if (sess->pairing_window_active && sess->pairing_window_deadline_ms > 0ULL) {
    if (now > sess->pairing_window_deadline_ms) {
      (void)sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_SC_ONLY
                               | SL_BT_SM_CONFIGURATION_BONDING_REQUIRED,
                               sl_bt_sm_io_capability_noinputnooutput);
      sess->pairing_window_active = false;
      phone_crypto_memzero(sess->pairing_window_id, 8U);
      sess->pairing_passkey = 0U;
      USER_LOG_INFO("[SESSION] PAIRING_WINDOW_TIMEOUT" USER_LOG_NL);
    }
  }
}
