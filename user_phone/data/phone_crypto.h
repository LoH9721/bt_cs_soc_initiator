/***************************************************************************//**
 * @file phone_crypto.h
 * @brief V1.1 密码算法模块：SHA-256、HMAC-SHA256、HKDF-SHA256、
 *        AES-128-CCM-8、ECDH P-256、ECDSA P-256、TRNG。
 *
 * 基于 PSA Crypto API (psa/crypto.h), 与 key_gatt_ecdsa 共用同一加密子系统。
 *
 * 公钥格式: 统一使用 65 字节未压缩格式 (04 || X || Y), 符合 V1.1 协议。
 * 签名格式: 64 字节 r || s (Big Endian)。
 ******************************************************************************/
#ifndef PHONE_CRYPTO_H
#define PHONE_CRYPTO_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// 初始化
// =============================================================================

/** 初始化 PSA Crypto (幂等, 多次调用无害) */
sl_status_t phone_crypto_init(void);

// =============================================================================
// 随机数 (TRNG)
// =============================================================================

/** 生成加密级随机数 */
sl_status_t phone_crypto_get_random(uint8_t *buf, uint16_t len);

// =============================================================================
// SHA-256
// =============================================================================

/** SHA-256 哈希 */
sl_status_t phone_crypto_sha256(const uint8_t *data, uint16_t len,
                                uint8_t hash[32]);

// =============================================================================
// HMAC-SHA256
// =============================================================================

/** HMAC-SHA256 */
sl_status_t phone_crypto_hmac_sha256(const uint8_t *key, uint16_t key_len,
                                     const uint8_t *data, uint16_t data_len,
                                     uint8_t mac[32]);

// =============================================================================
// HKDF-SHA256 (RFC 5869)
// =============================================================================

/**
 * @brief HKDF-SHA256 密钥派生
 * @param ikm        输入密钥材料
 * @param ikm_len    IKM 长度
 * @param salt       Salt (可为 NULL 表示全零 salt)
 * @param salt_len   Salt 长度
 * @param info       Info 上下文
 * @param info_len   Info 长度
 * @param okm        输出密钥材料
 * @param okm_len    期望输出长度 (≤ 32 * 255)
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_crypto_hkdf_sha256(const uint8_t *ikm, uint16_t ikm_len,
                                     const uint8_t *salt, uint16_t salt_len,
                                     const uint8_t *info, uint16_t info_len,
                                     uint8_t *okm, uint16_t okm_len);

// =============================================================================
// AES-128-CCM-8 (M=8, L=2)
// =============================================================================

/**
 * @brief AES-128-CCM 加密 (Tag=8 Byte, Nonce=13 Byte)
 * @param key          16 Byte AES 密钥
 * @param nonce        13 Byte Nonce
 * @param aad          附加认证数据
 * @param aad_len      AAD 长度
 * @param plaintext    明文
 * @param plaintext_len 明文长度
 * @param ciphertext   输出密文 (与明文等长)
 * @param tag          输出 Auth Tag (8 Byte)
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_crypto_aes_ccm_encrypt(const uint8_t key[16],
                                          const uint8_t nonce[13],
                                          const uint8_t *aad, uint16_t aad_len,
                                          const uint8_t *plaintext, uint16_t plaintext_len,
                                          uint8_t *ciphertext, uint8_t tag[8]);

/**
 * @brief AES-128-CCM 解密 + Tag 校验
 * @return SL_STATUS_OK 成功, SL_STATUS_INVALID_SIGNATURE Tag 校验失败
 */
sl_status_t phone_crypto_aes_ccm_decrypt(const uint8_t key[16],
                                          const uint8_t nonce[13],
                                          const uint8_t *aad, uint16_t aad_len,
                                          const uint8_t *ciphertext, uint16_t ciphertext_len,
                                          const uint8_t tag[8],
                                          uint8_t *plaintext);

// =============================================================================
// ECDH P-256
// =============================================================================

/**
 * @brief 生成临时 ECDH P-256 密钥对
 * @param pub_key  输出公钥 (65 Byte, 04||X||Y)
 * @param priv_key 输出私钥 (32 Byte) —— 仅驻留 RAM
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_crypto_ecdh_generate(uint8_t pub_key[65], uint8_t priv_key[32]);

/**
 * @brief ECDH P-256 共享密钥计算
 * @param priv_key     本方私钥 (32 Byte)
 * @param peer_pub_key 对方公钥 (65 Byte, 04||X||Y)
 * @param shared       输出共享密钥 (32 Byte)
 * @return SL_STATUS_OK 成功
 */
sl_status_t phone_crypto_ecdh_compute_shared(const uint8_t priv_key[32],
                                              const uint8_t peer_pub_key[65],
                                              uint8_t shared[32]);

// =============================================================================
// ECDSA P-256 验签
// =============================================================================

/**
 * @brief ECDSA P-256 验签 (SHA-256 digest, Prehashed 模式)
 * @param pub_key  公钥 (65 Byte, 04||X||Y)
 * @param digest   SHA-256 哈希值 (32 Byte)
 * @param sig      签名 (64 Byte, r||s BE)
 * @return SL_STATUS_OK 验签成功, SL_STATUS_INVALID_SIGNATURE 签名无效
 */
sl_status_t phone_crypto_ecdsa_verify(const uint8_t pub_key[65],
                                       const uint8_t digest[32],
                                       const uint8_t sig[64]);

// =============================================================================
// 安全内存清零
// =============================================================================

void phone_crypto_memzero(void *buf, size_t len);

// =============================================================================
// 协议专用密钥派生
// =============================================================================

/**
 * @brief 派生绑定会话密钥 bindSessionKey (第 9.1 节)
 *
 * bindSessionKey = HKDF-SHA256(
 *   ikm  = bindSecret,
 *   salt = appBindNonce || bgBindNonce,
 *   info = "BLEKEY-BIND-V1" || deviceId || qid || U32_BE(cv),
 *   L    = 16)
 */
sl_status_t phone_crypto_derive_bind_session_key(
    const uint8_t bind_secret[16],
    const uint8_t app_nonce[16], const uint8_t bg_nonce[16],
    const char *device_id, const char *qid, uint32_t cv,
    uint8_t key_out[16]);

/**
 * @brief 派生业务会话密钥 sessionKey (第 10 节)
 *
 * sessionKey = HKDF-SHA256(
 *   ikm  = ECDH shared secret,
 *   salt = nonceA || nonceB,
 *   info = "BLEKEY-AES-CCM-V1" || deviceId || appKeyId || authSessionId,
 *   L    = 16)
 */
sl_status_t phone_crypto_derive_session_key(
    const uint8_t shared_secret[32],
    const uint8_t nonce_a[16], const uint8_t nonce_b[16],
    const char *device_id, const uint8_t app_key_id[16],
    const uint8_t auth_session_id[16],
    uint8_t key_out[16]);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_CRYPTO_H */
