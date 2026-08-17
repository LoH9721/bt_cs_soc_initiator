/***************************************************************************//**
 * @file phone_crypto.c
 * @brief V1.1 密码算法模块实现：基于 PSA Crypto API。
 *
 * HMAC-SHA256 和 HKDF-SHA256 基于 SHA-256 原语手动实现,
 * 不依赖 PSA MAC/KeyDerivation 扩展 (以保证最大兼容性)。
 ******************************************************************************/

#include "user_phone/data/phone_crypto.h"
#include "user_phone/data/phone_frame.h"
#include "user_phone/phone_cfg.h"
#include "user_log_console.h"
#include "psa/crypto.h"
#include "sl_sleeptimer.h"
#include <stdio.h>
#include <string.h>

/* 本地 ms 计时 (基于 sl_sleeptimer, 与 phone_session_now_ms 同源) */
static uint32_t crypto_now_ms(void)
{
  uint64_t freq = sl_sleeptimer_get_timer_frequency();
  uint64_t tick = sl_sleeptimer_get_tick_count64();
  return (uint32_t)((tick * 1000ULL) / freq);
}

/* 本地 hex dump (debug only) */
static void crypto_hex_dump(const char *label, const uint8_t *data, uint16_t len)
{
  /* 512 = label + 3*255 + margin */
  static char buf[544];
  uint16_t i, pos = 0U;
  uint16_t limit = (len < 255U) ? len : 255U;
  for (i = 0U; i < limit && pos < sizeof(buf) - 6U; i++) {
    pos += (uint16_t)snprintf(&buf[pos], sizeof(buf) - pos, "%02X ", (unsigned)data[i]);
  }
  if (len > 255U) {
    pos += (uint16_t)snprintf(&buf[pos], sizeof(buf) - pos, "...(%u)", (unsigned)len);
  }
  USER_LOG_INFO("[CRYPTO] %s = %s" USER_LOG_NL, label, buf);
}

/* ========================================================================== */
/* 内部: 初始化 (幂等)                                                       */
/* ========================================================================== */

sl_status_t phone_crypto_init(void)
{
  psa_status_t status = psa_crypto_init();
  if (status != PSA_SUCCESS) {
    return SL_STATUS_FAIL;
  }
  return SL_STATUS_OK;
}

/* ========================================================================== */
/* 随机数                                                                     */
/* ========================================================================== */

sl_status_t phone_crypto_get_random(uint8_t *buf, uint16_t len)
{
  if (buf == NULL || len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }
  psa_status_t status = psa_generate_random(buf, (size_t)len);
  if (status != PSA_SUCCESS) {
    phone_crypto_memzero(buf, len);
    return SL_STATUS_FAIL;
  }
  return SL_STATUS_OK;
}

/* ========================================================================== */
/* SHA-256                                                                    */
/* ========================================================================== */

sl_status_t phone_crypto_sha256(const uint8_t *data, uint16_t len,
                                uint8_t hash[32])
{
  size_t out_len = 0U;
  psa_status_t status;

  if (data == NULL || hash == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  status = psa_hash_compute(PSA_ALG_SHA_256,
                            data, (size_t)len,
                            hash, 32U, &out_len);
  if (status != PSA_SUCCESS || out_len != 32U) {
    memset(hash, 0, 32U);
    return SL_STATUS_FAIL;
  }
  return SL_STATUS_OK;
}

/* ========================================================================== */
/* HMAC-SHA256 (手动实现, 仅需 SHA-256)                                       */
/* ========================================================================== */

#define HMAC_BLOCK_SIZE  64U   /* SHA-256 block size */
#define HMAC_IPAD        0x36U
#define HMAC_OPAD        0x5CU

sl_status_t phone_crypto_hmac_sha256(const uint8_t *key, uint16_t key_len,
                                     const uint8_t *data, uint16_t data_len,
                                     uint8_t mac[32])
{
  uint8_t k_pad[HMAC_BLOCK_SIZE];
  uint8_t inner_hash[32];
  uint8_t buf[HMAC_BLOCK_SIZE + 256];  /* 内层: k_pad⊕ipad || data */
  uint16_t i;

  if (key == NULL || mac == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 1. 密钥规整 */
  memset(k_pad, 0, sizeof(k_pad));
  if (key_len > HMAC_BLOCK_SIZE) {
    /* key > block size: hash it */
    sl_status_t sc = phone_crypto_sha256(key, key_len, k_pad);
    if (sc != SL_STATUS_OK) return sc;
  } else if (key_len > 0U) {
    memcpy(k_pad, key, key_len);
  }

  /* 2. 内层哈希: SHA256( (k_pad ⊕ ipad) || data ) */
  for (i = 0U; i < HMAC_BLOCK_SIZE; i++) {
    buf[i] = k_pad[i] ^ HMAC_IPAD;
  }
  if (data_len > 0U && data != NULL) {
    memcpy(&buf[HMAC_BLOCK_SIZE], data, data_len);
  }
  {
    sl_status_t sc = phone_crypto_sha256(buf, HMAC_BLOCK_SIZE + data_len, inner_hash);
    if (sc != SL_STATUS_OK) return sc;
  }

  /* 3. 外层哈希: SHA256( (k_pad ⊕ opad) || inner_hash ) */
  for (i = 0U; i < HMAC_BLOCK_SIZE; i++) {
    buf[i] = k_pad[i] ^ HMAC_OPAD;
  }
  memcpy(&buf[HMAC_BLOCK_SIZE], inner_hash, 32U);

  /* 清零 k_pad (含规整后的 key) */
  phone_crypto_memzero(k_pad, sizeof(k_pad));
  phone_crypto_memzero(inner_hash, 32U);

  return phone_crypto_sha256(buf, HMAC_BLOCK_SIZE + 32U, mac);
}

/* ========================================================================== */
/* HKDF-SHA256 (RFC 5869, 手动实现)                                           */
/* ========================================================================== */

sl_status_t phone_crypto_hkdf_sha256(const uint8_t *ikm, uint16_t ikm_len,
                                     const uint8_t *salt, uint16_t salt_len,
                                     const uint8_t *info, uint16_t info_len,
                                     uint8_t *okm, uint16_t okm_len)
{
  uint8_t prk[32];
  uint8_t t_prev[32];
  uint8_t t_cur[32];
  uint8_t counter;
  uint16_t offset = 0U;
  uint16_t copy_len;
  sl_status_t sc;

  if (ikm == NULL || okm == NULL || okm_len == 0U) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* HKDF-Extract: PRK = HMAC-SHA256(salt, IKM) */
  if (salt == NULL || salt_len == 0U) {
    /* Salt 为空时使用全零 salt */
    uint8_t zero_salt[32] = {0};
    sc = phone_crypto_hmac_sha256(zero_salt, 32U, ikm, ikm_len, prk);
  } else {
    sc = phone_crypto_hmac_sha256(salt, salt_len, ikm, ikm_len, prk);
  }
  if (sc != SL_STATUS_OK) {
    phone_crypto_memzero(prk, 32U);
    return sc;
  }

  /* HKDF-Expand: T(1) = HMAC-SHA256(PRK, info || 0x01)       */
  /*                T(i>1) = HMAC-SHA256(PRK, T(i-1) || info || i)  */
  /* T(0) = empty string per RFC 5869 §2.3 (NOT 32 zero bytes)     */
  memset(t_prev, 0, 32U);  /* unused for i=1; carries T(i-1) for i>1 */

  for (counter = 1U; offset < okm_len; counter++) {
    uint8_t expand_buf[32 + 256];  /* T_prev(32) + info(N) + counter(1) */
    uint16_t pos = 0U;

    /* T(0)=empty: skip prepend on first iteration */
    if (counter > 1U) {
      memcpy(&expand_buf[pos], t_prev, 32U);
      pos += 32U;
    }

    if (info_len > 0U && info != NULL) {
      memcpy(&expand_buf[pos], info, info_len);
      pos += info_len;
    }

    expand_buf[pos] = counter;
    pos++;

    sc = phone_crypto_hmac_sha256(prk, 32U, expand_buf, pos, t_cur);
    if (sc != SL_STATUS_OK) {
      phone_crypto_memzero(prk, 32U);
      phone_crypto_memzero(t_prev, 32U);
      return sc;
    }

    memcpy(t_prev, t_cur, 32U);

    /* 复制到 OKM */
    copy_len = (uint16_t)(okm_len - offset);
    if (copy_len > 32U) copy_len = 32U;
    memcpy(&okm[offset], t_cur, copy_len);
    offset += copy_len;
  }

  phone_crypto_memzero(prk, 32U);
  phone_crypto_memzero(t_prev, 32U);
  phone_crypto_memzero(t_cur, 32U);

  return SL_STATUS_OK;
}

/* ========================================================================== */
/* AES-128-CCM-8 (M=8, L=2)                                                   */
/* ========================================================================== */

static psa_key_id_t import_aes_key(const uint8_t key[16])
{
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&attr, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 8U));
  psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attr, 128U);

  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  psa_status_t status = psa_import_key(&attr, key, 16U, &key_id);
  if (status != PSA_SUCCESS) {
    return PSA_KEY_ID_NULL;
  }
  return key_id;
}

sl_status_t phone_crypto_aes_ccm_encrypt(const uint8_t key[16],
                                          const uint8_t nonce[13],
                                          const uint8_t *aad, uint16_t aad_len,
                                          const uint8_t *plaintext, uint16_t plaintext_len,
                                          uint8_t *ciphertext, uint8_t tag[8])
{
  psa_key_id_t key_id;
  size_t output_len = 0U;
  psa_status_t status;
  /* max output = max payload(236) + tag(8) = 244 */
  uint8_t output[PHONE_MAX_PAYLOAD_LEN + 8U];

  if (key == NULL || nonce == NULL || tag == NULL
      || (plaintext_len > 0U && (plaintext == NULL || ciphertext == NULL))) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  key_id = import_aes_key(key);
  if (key_id == PSA_KEY_ID_NULL) {
    return SL_STATUS_FAIL;
  }

  status = psa_aead_encrypt(
      key_id,
      PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 8U),
      nonce, 13U,
      aad, (size_t)aad_len,
      plaintext, (size_t)plaintext_len,
      output, sizeof(output),
      &output_len);

  psa_destroy_key(key_id);

  if (status != PSA_SUCCESS || output_len != (size_t)plaintext_len + 8U) {
    return SL_STATUS_FAIL;
  }

  /* PSA CCM 输出: ciphertext(plaintext_len) || tag(8) */
  if (plaintext_len > 0U) {
    memcpy(ciphertext, output, plaintext_len);
  }
  memcpy(tag, output + plaintext_len, 8U);
  phone_crypto_memzero(output, sizeof(output));

  return SL_STATUS_OK;
}

sl_status_t phone_crypto_aes_ccm_decrypt(const uint8_t key[16],
                                          const uint8_t nonce[13],
                                          const uint8_t *aad, uint16_t aad_len,
                                          const uint8_t *ciphertext, uint16_t ciphertext_len,
                                          const uint8_t tag[8],
                                          uint8_t *plaintext)
{
  psa_key_id_t key_id;
  size_t output_len = 0U;
  psa_status_t status;
  /* max input = max ciphertext(236) + tag(8) = 244 */
  uint8_t input[PHONE_MAX_PAYLOAD_LEN + 8U];  /* ciphertext + tag */

  if (key == NULL || nonce == NULL || tag == NULL || plaintext == NULL
      || (ciphertext_len > 0U && ciphertext == NULL)) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  key_id = import_aes_key(key);
  if (key_id == PSA_KEY_ID_NULL) {
    return SL_STATUS_FAIL;
  }

  /* 拼接 ciphertext + tag */
  if (ciphertext_len > 0U) {
    memcpy(input, ciphertext, ciphertext_len);
  }
  memcpy(input + ciphertext_len, tag, 8U);

  status = psa_aead_decrypt(
      key_id,
      PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 8U),
      nonce, 13U,
      aad, (size_t)aad_len,
      input, (size_t)ciphertext_len + 8U,
      plaintext, (size_t)ciphertext_len,
      &output_len);

  psa_destroy_key(key_id);

  if (status == PSA_ERROR_INVALID_SIGNATURE) {
    return SL_STATUS_INVALID_SIGNATURE;
  }
  if (status != PSA_SUCCESS) {
    return SL_STATUS_FAIL;
  }
  return SL_STATUS_OK;
}

/* ========================================================================== */
/* ECDH P-256                                                                 */
/* ========================================================================== */

sl_status_t phone_crypto_ecdh_generate(uint8_t pub_key[65], uint8_t priv_key[32])
{
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  psa_status_t status;
  size_t pub_len = 0U;

  if (pub_key == NULL || priv_key == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 生成临时密钥对 (RAM only, 不持久化) */
  psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&attr, 256U);
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE | PSA_KEY_USAGE_EXPORT);
  psa_set_key_algorithm(&attr, PSA_ALG_ECDH);

  status = psa_generate_key(&attr, &key_id);
  if (status != PSA_SUCCESS) {
    return SL_STATUS_FAIL;
  }

  /* 导出公钥 (65 Byte, 04||X||Y) */
  status = psa_export_public_key(key_id, pub_key, 65U, &pub_len);
  if (status != PSA_SUCCESS || pub_len != 65U) {
    psa_destroy_key(key_id);
    return SL_STATUS_FAIL;
  }

  /* 导出私钥 (PSA 不允许直接导出 private key, 除非显式设置 EXPORT 属性) */
  /* 使用 psa_export_key 导出密钥对的私钥部分 */
  status = psa_export_key(key_id, priv_key, 32U, &pub_len);
  psa_destroy_key(key_id);

  if (status != PSA_SUCCESS || pub_len != 32U) {
    phone_crypto_memzero(priv_key, 32U);
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}

sl_status_t phone_crypto_ecdh_compute_shared(const uint8_t priv_key[32],
                                              const uint8_t peer_pub_key[65],
                                              uint8_t shared[32])
{
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_id_t priv_id = PSA_KEY_ID_NULL;
  psa_status_t status;
  size_t output_len = 0U;

  if (priv_key == NULL || peer_pub_key == NULL || shared == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* 导入本方私钥 */
  psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&attr, 256U);
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_DERIVE);
  psa_set_key_algorithm(&attr, PSA_ALG_ECDH);

  status = psa_import_key(&attr, priv_key, 32U, &priv_id);
  if (status != PSA_SUCCESS) {
    return SL_STATUS_FAIL;
  }

  /* ECDH key agreement */
  status = psa_raw_key_agreement(PSA_ALG_ECDH, priv_id,
                                 peer_pub_key, 65U,
                                 shared, 32U, &output_len);

  psa_destroy_key(priv_id);

  if (status != PSA_SUCCESS || output_len != 32U) {
    phone_crypto_memzero(shared, 32U);
    return SL_STATUS_FAIL;
  }

  return SL_STATUS_OK;
}

/* ========================================================================== */
/* ECDSA P-256 验签                                                            */
/* ========================================================================== */

sl_status_t phone_crypto_ecdsa_verify(const uint8_t pub_key[65],
                                       const uint8_t digest[32],
                                       const uint8_t sig[64])
{
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_key_id_t peer_key_id = PSA_KEY_ID_NULL;
  psa_status_t import_status, verify_status;
  uint32_t t0, t1, t2;

  if (pub_key == NULL || digest == NULL || sig == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  t0 = crypto_now_ms();

#ifdef PHONE_DUMP_FRAMES
  /* ===== ECDSA 验签详细日志 ===== */
  USER_LOG_INFO("[CRYPTO] ===== ECDSA P-256 验签开始 =====" USER_LOG_NL);
  USER_LOG_INFO("[CRYPTO] 算法: PSA_ALG_ECDSA(PSA_ALG_SHA_256), 签名格式=RAW(r||s), 模式=prehashed" USER_LOG_NL);

  /* ---- 输入参数 dump ---- */
  crypto_hex_dump("pubkey[65]", pub_key, 65U);
  crypto_hex_dump("digest[32]", digest, 32U);
  crypto_hex_dump("sig[64]", sig, 64U);
#endif

  /* ---- Step A: 配置密钥属性 ---- */
  psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_HASH);
  psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
  psa_set_key_bits(&attr, 256U);

  /* ---- Step B: psa_import_key (加载 65 字节未压缩公钥) ---- */
  import_status = psa_import_key(&attr, pub_key, 65U, &peer_key_id);
  t1 = crypto_now_ms();
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[CRYPTO] psa_import_key status=0x%04lX key_id=%lu 耗时=%lums" USER_LOG_NL,
                (unsigned long)import_status, (unsigned long)peer_key_id,
                (unsigned long)(t1 - t0));

  if (import_status != PSA_SUCCESS) {
    USER_LOG_INFO("[CRYPTO] ❌ psa_import_key FAILED — 公钥导入失败" USER_LOG_NL);
    USER_LOG_INFO("[CRYPTO] ===== ECDSA 验签结束 (IMPORT_FAIL) =====" USER_LOG_NL);
    return SL_STATUS_FAIL;
  }
  USER_LOG_INFO("[CRYPTO] psa_import_key ✅ 成功" USER_LOG_NL);
#endif
  if (import_status != PSA_SUCCESS) {
    return SL_STATUS_FAIL;
  }

  /* ---- Step C: psa_verify_hash (ECDSA 验签) ---- */
  verify_status = psa_verify_hash(peer_key_id,
                                   PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                                   digest, 32U,
                                   sig, 64U);
  t2 = crypto_now_ms();
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[CRYPTO] psa_verify_hash status=0x%04lX 耗时=%lums (import耗时=%lums)" USER_LOG_NL,
                (unsigned long)verify_status,
                (unsigned long)(t2 - t1),
                (unsigned long)(t1 - t0));
#endif

  /* ---- Step D: 销毁临时密钥 ---- */
  {
    psa_status_t destroy_status = psa_destroy_key(peer_key_id);
#ifdef PHONE_DUMP_FRAMES
    if (destroy_status != PSA_SUCCESS) {
      USER_LOG_INFO("[CRYPTO] ⚠️ psa_destroy_key failed 0x%04lX (ignored)" USER_LOG_NL,
                    (unsigned long)destroy_status);
    }
#else
    (void)destroy_status;
#endif
  }

  /* ---- 结果判断 ---- */
  if (verify_status == PSA_SUCCESS) {
#ifdef PHONE_DUMP_FRAMES
    USER_LOG_INFO("[CRYPTO] psa_verify_hash ✅ 验签通过 总耗时=%lums" USER_LOG_NL,
                  (unsigned long)(t2 - t0));
    USER_LOG_INFO("[CRYPTO] ===== ECDSA 验签结束 (OK) =====" USER_LOG_NL);
#endif
    return SL_STATUS_OK;
  }

  if (verify_status == PSA_ERROR_INVALID_SIGNATURE) {
#ifdef PHONE_DUMP_FRAMES
    USER_LOG_INFO("[CRYPTO] ❌ psa_verify_hash = PSA_ERROR_INVALID_SIGNATURE (签名不匹配) 总耗时=%lums" USER_LOG_NL,
                  (unsigned long)(t2 - t0));
    USER_LOG_INFO("[CRYPTO] ===== ECDSA 验签结束 (INVALID_SIGNATURE) =====" USER_LOG_NL);
#endif
    return SL_STATUS_INVALID_SIGNATURE;
  }

  /* 其他错误码 */
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[CRYPTO] ❌ psa_verify_hash = 0x%04lX (其他PSA错误) 总耗时=%lums" USER_LOG_NL,
                (unsigned long)verify_status, (unsigned long)(t2 - t0));
  USER_LOG_INFO("[CRYPTO] ===== ECDSA 验签结束 (FAIL) =====" USER_LOG_NL);
#endif
  return SL_STATUS_FAIL;
}

/* ========================================================================== */
/* 安全内存清零                                                                */
/* ========================================================================== */

void phone_crypto_memzero(void *buf, size_t len)
{
  if (buf == NULL || len == 0U) {
    return;
  }
  volatile uint8_t *p = (volatile uint8_t *)buf;
  while (len != 0U) {
    *p = 0U;
    p++;
    len--;
  }
}

/* ========================================================================== */
/* 协议专用密钥派生                                                            */
/* ========================================================================== */

sl_status_t phone_crypto_derive_bind_session_key(
    const uint8_t bind_secret[16],
    const uint8_t app_nonce[16], const uint8_t bg_nonce[16],
    const char *device_id, const char *qid, uint32_t cv,
    uint8_t key_out[16])
{
  uint8_t salt[32];  /* appBindNonce(16) || bgBindNonce(16) */
  uint8_t info[256];
  uint16_t info_len = 0U;
  uint8_t cv_buf[4];

  if (bind_secret == NULL || app_nonce == NULL || bg_nonce == NULL
      || device_id == NULL || qid == NULL || key_out == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

  /* salt = appBindNonce || bgBindNonce */
  memcpy(salt, app_nonce, 16U);
  memcpy(salt + 16U, bg_nonce, 16U);

  /* info = "BLEKEY-BIND-V1" || deviceId || qid || U32_BE(cv) */
  memcpy(info + info_len, PHONE_HKDF_INFO_BIND, PHONE_HKDF_INFO_BIND_LEN);
  info_len += PHONE_HKDF_INFO_BIND_LEN;

  {
    uint16_t did_len = (uint16_t)strlen(device_id);
    memcpy(info + info_len, device_id, did_len);
    info_len += did_len;
  }

  {
    uint16_t qid_len = (uint16_t)strlen(qid);
    memcpy(info + info_len, qid, qid_len);
    info_len += qid_len;
  }

  phone_frame_write_u32_be(cv_buf, cv);
  memcpy(info + info_len, cv_buf, 4U);
  info_len += 4U;

  return phone_crypto_hkdf_sha256(bind_secret, 16U,
                                  salt, 32U,
                                  info, info_len,
                                  key_out, 16U);
}

sl_status_t phone_crypto_derive_session_key(
    const uint8_t shared_secret[32],
    const uint8_t nonce_a[16], const uint8_t nonce_b[16],
    const char *device_id, const uint8_t app_key_id[16],
    const uint8_t auth_session_id[16],
    uint8_t key_out[16])
{
  uint8_t salt[32];  /* nonceA(16) || nonceB(16) */
  uint8_t info[256];
  uint16_t info_len = 0U;
  sl_status_t sc;

  if (shared_secret == NULL || nonce_a == NULL || nonce_b == NULL
      || device_id == NULL || app_key_id == NULL
      || auth_session_id == NULL || key_out == NULL) {
    return SL_STATUS_INVALID_PARAMETER;
  }

#ifdef PHONE_DUMP_FRAMES
  /* ===== SessionKey 派生详细日志 (APP 对照用) ===== */
  USER_LOG_INFO("[CRYPTO] ===== HKDF SessionKey 派生开始 =====" USER_LOG_NL);
  USER_LOG_INFO("[CRYPTO] HKDF info_prefix = \"%s\" (len=%u)" USER_LOG_NL,
                PHONE_HKDF_INFO_SESSION, PHONE_HKDF_INFO_SESSION_LEN);

  /* ---- 输入 #1: ECDH shared_secret (IKM) ---- */
  crypto_hex_dump("HKDF.IKM(shared_secret[32])", shared_secret, 32U);

  /* ---- 输入 #2,3: nonceA, nonceB (组成 salt) ---- */
  crypto_hex_dump("HKDF.nonce_a[16]", nonce_a, 16U);
  crypto_hex_dump("HKDF.nonce_b[16]", nonce_b, 16U);

  /* ---- 输入 #4: deviceId (字符串) ---- */
  USER_LOG_INFO("[CRYPTO] HKDF.deviceId = \"%s\" (strlen=%u)" USER_LOG_NL,
                device_id, (unsigned)strlen(device_id));

  /* ---- 输入 #5: appKeyId ---- */
  crypto_hex_dump("HKDF.app_key_id[16]", app_key_id, 16U);

  /* ---- 输入 #6: authSessionId ---- */
  crypto_hex_dump("HKDF.auth_session_id[16]", auth_session_id, 16U);
#endif

  /* salt = nonceA || nonceB */
  memcpy(salt, nonce_a, 16U);
  memcpy(salt + 16U, nonce_b, 16U);

#ifdef PHONE_DUMP_FRAMES
  crypto_hex_dump("HKDF.salt(nonceA||nonceB[32])", salt, 32U);
#endif

  /* info = "BLEKEY-AES-CCM-V1" || deviceId || appKeyId || authSessionId */
  memcpy(info + info_len, PHONE_HKDF_INFO_SESSION, PHONE_HKDF_INFO_SESSION_LEN);
  info_len += PHONE_HKDF_INFO_SESSION_LEN;

  {
    uint16_t did_len = (uint16_t)strlen(device_id);
    memcpy(info + info_len, device_id, did_len);
    info_len += did_len;
  }

  memcpy(info + info_len, app_key_id, 16U);
  info_len += 16U;

  memcpy(info + info_len, auth_session_id, 16U);
  info_len += 16U;

#ifdef PHONE_DUMP_FRAMES
  crypto_hex_dump("HKDF.info (full)", info, info_len);
#endif

  sc = phone_crypto_hkdf_sha256(shared_secret, 32U,
                                  salt, 32U,
                                  info, info_len,
                                  key_out, 16U);

  /* ---- 输出: sessionKey ---- */
#ifdef PHONE_DUMP_FRAMES
  if (sc == SL_STATUS_OK) {
    crypto_hex_dump("HKDF.session_key[16]", key_out, 16U);
    USER_LOG_INFO("[CRYPTO] ===== HKDF SessionKey 派生 OK =====" USER_LOG_NL);
  } else {
    USER_LOG_INFO("[CRYPTO] ===== HKDF SessionKey 派生 FAIL sc=0x%04lX =====" USER_LOG_NL,
                  (unsigned long)sc);
  }
#endif

  return sc;
}
