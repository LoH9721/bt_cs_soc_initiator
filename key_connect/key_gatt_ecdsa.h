/***************************************************************************//**
 * @file key_gatt_ecdsa.h
 * @brief 密码算法模块对外接口：SHA-256、AES-128-CCM、ECDSA P-256、TRNG、
 *        会话密钥派生、安全内存清零
 ******************************************************************************/
#ifndef KEY_GATT_ECDSA_H
#define KEY_GATT_ECDSA_H

/* 控制器侧定义 KEY_ROLE_CONTROLLER */
#ifndef KEY_ROLE_CONTROLLER
#define KEY_ROLE_CONTROLLER  1
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// 协议版本号
#define PROTO_MIN_SUPPORTED_VER  0x01

// PSA 持久化密钥 ID (本地 P-256 密钥对)
#define PROTO_LOCAL_KEY_ID  0x1005

// ===== 随机数生成 =====

void key_crypto_get_random(uint8_t *buf, uint8_t len);

// ===== SHA-256 =====

void key_crypto_sha256(const uint8_t *data, uint16_t len, uint8_t hash[32]);

// ===== AES-128-CCM (M=4, L=2) =====

bool key_crypto_aes_ccm_encrypt(
    const uint8_t *key, const uint8_t *nonce,
    const uint8_t *plaintext, uint16_t plaintext_len,
    const uint8_t *aad, uint16_t aad_len,
    uint8_t *ciphertext, uint8_t mic[4]);

bool key_crypto_aes_ccm_decrypt(
    const uint8_t *key, const uint8_t *nonce,
    const uint8_t *ciphertext, uint16_t ciphertext_len,
    const uint8_t *aad, uint16_t aad_len,
    const uint8_t mic[4], uint8_t *plaintext);

// ===== ECDSA P-256 =====

// 初始化 PSA Crypto 并生成/加载本地 P-256 密钥对 (首次上电自动生成)
bool key_crypto_init(void);

// 获取本地 P-256 公钥 64 字节 (x || y, Big-Endian, 不含 0x04 前缀)
bool key_crypto_get_local_pubkey(uint8_t pubkey[64]);

// ECDSA P-256 签名 (使用 SE 中持久化密钥, 私钥不离开 SE)
void key_crypto_ecdsa_sign_by_id(
    uint32_t key_id, const uint8_t *hash, uint8_t signature[64]);

// ECDSA P-256 验签 (public_key 为 64 字节 x||y Big-Endian)
bool key_crypto_ecdsa_verify(
    const uint8_t *public_key, const uint8_t *hash, const uint8_t signature[64]);

// ===== 会话密钥派生 =====

void key_crypto_derive_session_key(
    const uint8_t *key1, const uint8_t *key2, uint8_t session_key[16]);

// ===== 安全内存清零 =====

void key_crypto_memzero(void *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* KEY_GATT_ECDSA_H */
