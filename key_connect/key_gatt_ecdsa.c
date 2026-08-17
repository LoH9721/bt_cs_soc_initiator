/***************************************************************************//**
 * @file key_gatt_ecdsa.c
 * @brief 密码算法模块实现：基于 PSA Crypto API 的 SHA-256、AES-128-CCM、
 *        ECDSA P-256、TRNG 随机数、会话密钥派生
 ******************************************************************************/
#include "key_gatt_ecdsa.h"
#include "psa/crypto.h"
#include "user_log_console.h"
#include <string.h>

// 本地 P-256 密钥对的 PSA key_id (持久化)
static mbedtls_svc_key_id_t local_ecdsa_key_id = PSA_KEY_ID_NULL;

// ===== 安全内存清零 =====

void key_crypto_memzero(void *buf, size_t len)
{
  if (buf == NULL || len == 0U) return;
  volatile uint8_t *p = (volatile uint8_t *)buf;
  while (len--) { *p++ = 0U; }
}

// ===== 随机数生成 (TRNG) =====

void key_crypto_get_random(uint8_t *buf, uint8_t len)
{
  if (buf == NULL || len == 0U) return;
  psa_status_t status = psa_generate_random(buf, (size_t)len);
  if (status != PSA_SUCCESS) {
    memset(buf, 0, len);
  }
}

// ===== SHA-256 =====

void key_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t hash[32])
{
  psa_hash_operation_t op = psa_hash_operation_init();
  psa_status_t status = psa_hash_setup(&op, PSA_ALG_SHA_256);
  if (status == PSA_SUCCESS) status = psa_hash_update(&op, data, (size_t)len);
  size_t hash_len = 0U;
  if (status == PSA_SUCCESS) status = psa_hash_finish(&op, hash, 32U, &hash_len);
  if (status != PSA_SUCCESS || hash_len != 32U) {
    USER_LOG_ERROR("[CRYPTO] SHA-256 FAIL: status=0x%04lx len=%u" USER_LOG_NL,
                   (unsigned long)status, (unsigned)hash_len);
    psa_hash_abort(&op);
    memset(hash, 0, 32);
  }
}

// ===== AES-128-CCM (M=4, L=2) =====

static psa_key_id_t import_aes_key(const uint8_t *key)
{
  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_ENCRYPT | PSA_KEY_USAGE_DECRYPT);
  psa_set_key_algorithm(&attr, PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4U));
  psa_set_key_type(&attr, PSA_KEY_TYPE_AES);
  psa_set_key_bits(&attr, 128U);

  psa_key_id_t key_id = PSA_KEY_ID_NULL;
  psa_status_t status = psa_import_key(&attr, key, 16U, &key_id);
  if (status != PSA_SUCCESS) {
    USER_LOG_ERROR("[CRYPTO] AES import_key FAIL: 0x%04lx" USER_LOG_NL, (unsigned long)status);
    return PSA_KEY_ID_NULL;
  }
  return key_id;
}

bool key_crypto_aes_ccm_encrypt(
    const uint8_t *key, const uint8_t *nonce,
    const uint8_t *plaintext, uint16_t plaintext_len,
    const uint8_t *aad, uint16_t aad_len,
    uint8_t *ciphertext, uint8_t mic[4])
{
  if (key == NULL || nonce == NULL || plaintext == NULL
      || ciphertext == NULL || mic == NULL) return false;

  psa_key_id_t key_id = import_aes_key(key);
  if (key_id == PSA_KEY_ID_NULL) return false;

  const psa_algorithm_t alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4U);

  size_t output_len = 0U;
  psa_status_t status = psa_aead_encrypt(
      key_id, alg,
      nonce, 13U,
      aad, (size_t)aad_len,
      plaintext, (size_t)plaintext_len,
      ciphertext, (size_t)(plaintext_len + 4U),
      &output_len);

  psa_destroy_key(key_id);

  if (status != PSA_SUCCESS || output_len < 4U) {
    USER_LOG_ERROR("[CRYPTO] CCM encrypt FAIL: status=0x%04lx out_len=%u" USER_LOG_NL,
                   (unsigned long)status, (unsigned)output_len);
    return false;
  }

  // PSA CCM 输出: ciphertext || tag, 最后 4 字节为 MIC
  memcpy(mic, ciphertext + output_len - 4U, 4U);
  return true;
}

bool key_crypto_aes_ccm_decrypt(
    const uint8_t *key, const uint8_t *nonce,
    const uint8_t *ciphertext, uint16_t ciphertext_len,
    const uint8_t *aad, uint16_t aad_len,
    const uint8_t mic[4], uint8_t *plaintext)
{
  if (key == NULL || nonce == NULL || ciphertext == NULL
      || mic == NULL || plaintext == NULL) return false;

  psa_key_id_t key_id = import_aes_key(key);
  if (key_id == PSA_KEY_ID_NULL) return false;

  const psa_algorithm_t alg = PSA_ALG_AEAD_WITH_SHORTENED_TAG(PSA_ALG_CCM, 4U);

  // 拼接 ciphertext + MIC 传给 PSA
  uint8_t input[256];
  if ((size_t)ciphertext_len + 4U > sizeof(input)) {
    psa_destroy_key(key_id);
    return false;
  }
  memcpy(input, ciphertext, ciphertext_len);
  memcpy(input + ciphertext_len, mic, 4U);

  size_t output_len = 0U;
  psa_status_t status = psa_aead_decrypt(
      key_id, alg,
      nonce, 13U,
      aad, (size_t)aad_len,
      input, (size_t)ciphertext_len + 4U,
      plaintext, (size_t)ciphertext_len,
      &output_len);

  psa_destroy_key(key_id);
  if (status != PSA_SUCCESS) {
    USER_LOG_ERROR("[CRYPTO] CCM decrypt FAIL: status=0x%04lx" USER_LOG_NL, (unsigned long)status);
  }
  return (status == PSA_SUCCESS);
}

// ===== ECDSA P-256 (完整实现，使用 PSA Crypto + SE) =====

bool key_crypto_init(void)
{
  psa_status_t status;

  // 初始化 PSA Crypto (幂等，多次调用无害)
  status = psa_crypto_init();
  if (status != PSA_SUCCESS) {
    USER_LOG_ERROR("[CRYPTO] psa_crypto_init failed 0x%04lx" USER_LOG_NL,
                   (unsigned long)status);
    return false;
  }

  // 尝试打开已存在的持久化密钥对，校验算法一致性
  status = psa_open_key(PROTO_LOCAL_KEY_ID, &local_ecdsa_key_id);
  if (status == PSA_SUCCESS) {
    psa_key_attributes_t existing_attr = PSA_KEY_ATTRIBUTES_INIT;
    psa_get_key_attributes(local_ecdsa_key_id, &existing_attr);
    psa_algorithm_t existing_alg = psa_get_key_algorithm(&existing_attr);
    psa_reset_key_attributes(&existing_attr);
    psa_algorithm_t expected = PSA_ALG_ECDSA(PSA_ALG_SHA_256);
    if (existing_alg != expected) {
      USER_LOG_INFO("[CRYPTO] key alg mismatch (old=0x%08lx new=0x%08lx), destroying and regenerating..." USER_LOG_NL,
                    (unsigned long)existing_alg, (unsigned long)expected);
      psa_destroy_key(local_ecdsa_key_id);
      local_ecdsa_key_id = PSA_KEY_ID_NULL;
    } else {
      USER_LOG_INFO("[CRYPTO] ECDSA key pair loaded (id=0x%04x)" USER_LOG_NL, PROTO_LOCAL_KEY_ID);
      return true;
    }
  }

  // 密钥对不存在，生成新的 P-256 密钥对
  USER_LOG_INFO("[CRYPTO] generating new P-256 key pair..." USER_LOG_NL);

  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_KEY_PAIR(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_bits(&attr, 256U);
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_SIGN_HASH | PSA_KEY_USAGE_EXPORT);
  psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
  psa_set_key_lifetime(&attr, PSA_KEY_LIFETIME_PERSISTENT);
  psa_set_key_id(&attr, PROTO_LOCAL_KEY_ID);

  status = psa_generate_key(&attr, &local_ecdsa_key_id);
  if (status != PSA_SUCCESS) {
    USER_LOG_ERROR("[CRYPTO] psa_generate_key failed 0x%04lx" USER_LOG_NL,
                   (unsigned long)status);
    return false;
  }

  USER_LOG_INFO("[CRYPTO] P-256 key pair generated (id=0x%04x)" USER_LOG_NL,
                PROTO_LOCAL_KEY_ID);
  return true;
}

bool key_crypto_get_local_pubkey(uint8_t pubkey[64])
{
  if (local_ecdsa_key_id == PSA_KEY_ID_NULL) return false;

  uint8_t buf_65[65];
  size_t pubkey_len = 0U;
  psa_status_t status = psa_export_public_key(local_ecdsa_key_id,
                                               buf_65, sizeof(buf_65),
                                               &pubkey_len);
  if (status != PSA_SUCCESS || pubkey_len != 65U) return false;

  // PSA 导出格式: 0x04 || x || y (65 字节), 去掉 0x04 前缀
  memcpy(pubkey, buf_65 + 1, 64);
  return true;
}

void key_crypto_ecdsa_sign_by_id(
    uint32_t key_id, const uint8_t *hash, uint8_t signature[64])
{
  if (hash == NULL || signature == NULL) return;

  psa_key_id_t use_key = local_ecdsa_key_id;
  (void)key_id;

  if (use_key == PSA_KEY_ID_NULL) {
    memset(signature, 0, 64);
    return;
  }

  size_t sig_len = 0U;
  psa_status_t status = psa_sign_hash(
      use_key,
      PSA_ALG_ECDSA(PSA_ALG_SHA_256),
      hash, 32U,
      signature, 64U,
      &sig_len);

  if (status != PSA_SUCCESS || sig_len != 64U) {
    USER_LOG_ERROR("[CRYPTO] ECDSA sign failed 0x%04lx" USER_LOG_NL,
                   (unsigned long)status);
    memset(signature, 0, 64);
  }
}

bool key_crypto_ecdsa_verify(
    const uint8_t *public_key, const uint8_t *hash, const uint8_t signature[64])
{
  if (public_key == NULL || hash == NULL || signature == NULL) return false;

  // 导入对端公钥 (prepend 0x04 前缀形成未压缩格式)
  uint8_t pubkey_uncompressed[65];
  pubkey_uncompressed[0] = 0x04;
  memcpy(pubkey_uncompressed + 1, public_key, 64);

  psa_key_attributes_t attr = PSA_KEY_ATTRIBUTES_INIT;
  psa_set_key_type(&attr, PSA_KEY_TYPE_ECC_PUBLIC_KEY(PSA_ECC_FAMILY_SECP_R1));
  psa_set_key_usage_flags(&attr, PSA_KEY_USAGE_VERIFY_HASH);
  psa_set_key_algorithm(&attr, PSA_ALG_ECDSA(PSA_ALG_SHA_256));
  psa_set_key_bits(&attr, 256U);

  psa_key_id_t peer_key_id = PSA_KEY_ID_NULL;
  psa_status_t status = psa_import_key(&attr, pubkey_uncompressed, 65U, &peer_key_id);
  if (status != PSA_SUCCESS) {
    USER_LOG_ERROR("[CRYPTO] import peer pubkey failed 0x%04lx" USER_LOG_NL,
                   (unsigned long)status);
    return false;
  }

  status = psa_verify_hash(peer_key_id,
                           PSA_ALG_ECDSA(PSA_ALG_SHA_256),
                           hash, 32U,
                           signature, 64U);

  psa_destroy_key(peer_key_id);

  if (status == PSA_ERROR_INVALID_SIGNATURE) {
    USER_LOG_ERROR("[CRYPTO] ECDSA verify: invalid signature" USER_LOG_NL);
    return false;
  }
  if (status != PSA_SUCCESS) {
    USER_LOG_ERROR("[CRYPTO] ECDSA verify error 0x%04lx" USER_LOG_NL,
                   (unsigned long)status);
    return false;
  }
  return true;
}

// ===== 会话密钥派生 =====

void key_crypto_derive_session_key(
    const uint8_t *key1, const uint8_t *key2, uint8_t session_key[16])
{
  uint8_t combined[32];
  memcpy(combined, key1, 16U);
  memcpy(combined + 16U, key2, 16U);

  uint8_t hash[32];
  key_crypto_sha256(combined, 32U, hash);
  memcpy(session_key, hash, 16U);
}
