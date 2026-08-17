/***************************************************************************//**
 * @file phone_session.h
 * @brief V1.1 会话管理：绑定会话、认证会话、控制 challenge、
 *        SecurityCounter、幂等缓存、安全失败/SILENT 管理。
 *
 * 所有数据仅驻留 RAM, BLE 断开即销毁。
 ******************************************************************************/
#ifndef PHONE_SESSION_H
#define PHONE_SESSION_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// 会话状态结构体
// =============================================================================

typedef struct {
  /* ---- 绑定临时会话 (BIND_HELLO 后建立, 60s 有效) ---- */
  uint8_t  bind_session_id[16];   /* BG24 生成的 bindSessionId */
  uint8_t  bind_session_key[16];  /* bindSessionKey (RAM only) */
  uint8_t  app_bind_nonce[16];    /* APP 发送的 appBindNonce */
  uint8_t  bg_bind_nonce[16];     /* BG24 生成的 bgBindNonce */
  uint64_t bind_deadline_ms;      /* 绑定会话过期时间戳 (ms) */
  bool     bind_session_active;   /* 绑定会话是否有效 */
  bool     qr_verified;           /* QR_VERIFY 是否通过 */
  bool     authorization_met;     /* BIND_WINDOW_QUERY 是否通过 */

  /* V1.2: 首次绑定候选 VIN (BIND_WINDOW_QUERY 暂存, RAM only, 断连即失效) */
  uint8_t  candidate_vin[17];     /* 候选 VIN (17 字节 ASCII) */
  bool     candidate_vin_valid;   /* 候选 VIN 是否有效 */

  /* V1.2 PASSIVE_PAIR: 配对上下文 (协议 §23.2) */
  uint8_t  pairing_window_id[8];           /* TRNG 生成的 8 字节随机窗口 ID */
  uint32_t pairing_passkey;                /* 6 位 Passkey (000000~999999) */
  uint64_t pairing_window_deadline_ms;     /* 窗口过期时间戳 (30s PREPARE / 60s READY) */
  bool     pairing_window_active;          /* 配对窗口是否有效 */
  bool     pairing_awaiting_system;        /* READY 后等待系统 Pairing (断连不销毁窗口) */
  uint8_t  pairing_fail_count_this_session;/* 本 AUTH session 连续配对失败次数 (上限 3) */

  /* V1.2 PASSIVE: 无感钥匙状态 */
  bool     passive_enabled;               /* 无感钥匙是否启用 (持久化) */
  bool     hid_runtime_enabled;           /* HID runtime 是否运行 (transient) */
  uint8_t  passive_sensitivity;           /* 灵敏度档位 (持久化, PHONE_PASSIVE_SENS_*) */
  uint32_t passive_quota_remaining;       /* 自动解锁剩余额度 (transient, 不持久化) */

  /* ---- 认证会话 (AUTH_CHALLENGE_REQ 后建立) ---- */
  uint8_t  auth_session_id[16];   /* BG24 生成的 authSessionId */
  uint8_t  challenge_id[16];      /* BG24 生成的 challengeId */
  uint8_t  nonce_a[16];           /* APP 发送的 nonceA */
  uint8_t  nonce_b[16];           /* BG24 生成的 nonceB */
  uint8_t  app_ecdh_pubkey[65];   /* APP 临时 ECDH 公钥 */
  uint8_t  bg_ecdh_privkey[32];   /* BG24 临时 ECDH 私钥 (RAM only) */
  uint8_t  bg_ecdh_pubkey[65];    /* BG24 临时 ECDH 公钥 */
  uint8_t  session_key[16];       /* 业务 sessionKey (RAM only) */
  uint64_t auth_deadline_ms;      /* 认证 challenge 过期时间 */
  bool     auth_challenge_active; /* 是否有待验证的 AUTH challenge */
  bool     auth_done;             /* 认证是否已完成 (sessionKey 有效) */

  /* ---- 控制会话 ---- */
  uint8_t  ctrl_challenge_id[16]; /* 控制 challengeId */
  uint8_t  ctrl_nonce[16];        /* 控制 nonce */
  uint64_t ctrl_deadline_ms;      /* 控制 challenge 过期时间 */
  bool     ctrl_challenge_active; /* 是否有有效的控制 challenge */

  /* ---- 计数器 ---- */
  uint64_t security_counter_rx;   /* APP→BG24, 最后接受的值 */
  uint64_t security_counter_tx;   /* BG24→APP, 下一个发送值 */
  uint16_t seq_rx;                /* 最近收到的 Seq (幂等查询用) */
  uint16_t seq_tx;                /* 下一个 Seq */

  /* ---- 幂等缓存 (CTRL_COMMAND 响应缓存) ---- */
  uint8_t  idem_cmd;              /* 缓存的 Cmd */
  uint16_t idem_seq;              /* 缓存的 Seq */
  uint8_t  idem_response[256];    /* 缓存的完整响应字节流 */
  uint16_t idem_response_len;     /* 响应长度 */
  uint64_t idem_deadline_ms;      /* 缓存过期时间 */

  /* ---- 安全失败 / SILENT ---- */
  uint8_t  security_fail_count;   /* 连续安全失败次数 */
  uint64_t silent_until_ms;       /* SILENT 结束时间戳 */
  bool     is_silent;             /* 当前是否 SILENT */

  /* ---- 状态稳定延迟 (STATE_CHANGED_EVENT, 协议 §13: 100~300ms) ---- */
  uint8_t  pending_lock_state;    /* 待推送的锁状态 (0=无) */
  bool     pending_vehicle_state; /* V1.2: TRUE=有车辆状态变化待推送 (完整快照) */
  uint64_t lock_state_deadline_ms; /* 稳定截止时间 */

  /* ---- MTU ---- */
  uint16_t att_mtu;               /* 协商后的 ATT MTU */

  /* ---- 连接句柄 ---- */
  uint8_t  conn_handle;           /* BLE 连接句柄 */

  /* ---- 当前设备状态 (bindState) ---- */
  uint8_t  device_state;          /* 从 NVM 读取/更新 */
} phone_session_t;

// =============================================================================
// 生命周期
// =============================================================================

/** 初始化会话结构体, 从 NVM 加载持久化状态 */
void phone_session_init(phone_session_t *sess);

/** BLE 断连时重置所有 RAM 状态 (appCounter 和 bindState 从 NVM 重新加载) */
void phone_session_reset(phone_session_t *sess);

// =============================================================================
// 绑定会话
// =============================================================================

/**
 * @brief 开始绑定会话 (BIND_HELLO 成功时调用)
 * @param sess           会话
 * @param app_nonce      APP 发送的 appBindNonce
 * @param bg_nonce       BG24 生成的 bgBindNonce
 * @param session_id     BG24 生成的 bindSessionId
 * @param session_key    派生出的 bindSessionKey (16 Byte)
 * @param remain_sec     会话有效期 (秒)
 */
void phone_session_begin_bind(phone_session_t *sess,
                              const uint8_t app_nonce[16],
                              const uint8_t bg_nonce[16],
                              const uint8_t session_id[16],
                              const uint8_t session_key[16],
                              uint16_t remain_sec);

/** 绑定会话是否仍然有效 (在超时前) */
bool phone_session_is_bind_active(const phone_session_t *sess);

/** 标记 QR_VERIFY 通过 */
void phone_session_set_qr_verified(phone_session_t *sess);

/** 标记 BIND_WINDOW_QUERY 通过 */
void phone_session_set_authorized(phone_session_t *sess);

/** 终止绑定会话 (超时/断连/完成绑定后) */
void phone_session_end_bind(phone_session_t *sess);

// =============================================================================
// 已绑定认证会话
// =============================================================================

/**
 * @brief 开始 AUTH challenge (AUTH_CHALLENGE_REQ 处理时调用)
 * 生成 bgEcdhPrivKey, bgEcdhPubKey, nonceB, authSessionId, challengeId
 * 注意: appEcdhPublicKey 和 nonceA 在 AUTH_CHALLENGE_RSP 中由 APP 传入
 */
sl_status_t phone_session_begin_auth(phone_session_t *sess,
                                     const uint8_t app_key_id[16]);

/** AUTH challenge 是否有效 */
bool phone_session_is_auth_challenge_active(const phone_session_t *sess);

/**
 * @brief 完成 AUTH (AUTH_CHALLENGE_RSP 成功时调用)
 * 派生 sessionKey, 设置 auth_done=true, 清除 challenge
 */
sl_status_t phone_session_complete_auth(phone_session_t *sess,
                                        const uint8_t session_key[16]);

/** 认证是否已完成 */
bool phone_session_is_authenticated(const phone_session_t *sess);

// =============================================================================
// 控制 challenge
// =============================================================================

/** 创建控制 challenge (10s 有效) */
sl_status_t phone_session_begin_ctrl_challenge(phone_session_t *sess);

/** 消耗控制 challenge (首次非缓存使用后作废) */
bool phone_session_consume_ctrl_challenge(phone_session_t *sess);

/** 控制 challenge 是否有效 */
bool phone_session_is_ctrl_challenge_active(const phone_session_t *sess);

// =============================================================================
// 计数器
// =============================================================================

/** 获取并递增 TX SecurityCounter (BG24→APP) */
uint64_t phone_session_next_tx_counter(phone_session_t *sess);

/** 获取并递增 Seq (BG24→APP Response) */
uint16_t phone_session_next_tx_seq(phone_session_t *sess);

/**
 * @brief 检查接收到的 SecurityCounter (APP→BG24)
 * @return SL_STATUS_OK 通过; 否则安全错误
 */
sl_status_t phone_session_check_rx_counter(phone_session_t *sess, uint64_t counter);

/** 接受接收到的 SecurityCounter (更新 last_accepted_rx) */
void phone_session_accept_rx_counter(phone_session_t *sess, uint64_t counter);

// =============================================================================
// 幂等缓存
// =============================================================================

/**
 * @brief 查询幂等缓存
 * @param cmd          命令码
 * @param seq          序列号
 * @param response_buf 输出: 缓存响应字节流 (命中时)
 * @param response_len 输出: 响应长度
 * @return true 命中缓存 (response 有效), false 未命中
 */
bool phone_session_check_idempotent(phone_session_t *sess, uint8_t cmd, uint16_t seq,
                                    uint8_t *response_buf, uint16_t *response_len);

/** 缓存幂等响应 */
void phone_session_cache_idempotent(phone_session_t *sess, uint8_t cmd, uint16_t seq,
                                    const uint8_t *response, uint16_t response_len);

// =============================================================================
// 安全失败 / SILENT
// =============================================================================

/** 记录一次安全失败, 达到阈值自动进入 SILENT */
void phone_session_record_security_failure(phone_session_t *sess);

/** 清除 SILENT 状态并重置失败计数 (成功认证后) */
void phone_session_clear_silent(phone_session_t *sess);

/** 清除安全失败计数但不退出 SILENT (用于非 SILENT 场景) */
void phone_session_clear_security_failures(phone_session_t *sess);

// =============================================================================
// 超时检查 (每个 process_action 周期调用)
// =============================================================================

/** 检查并处理所有超时: 绑定会话、AUTH challenge、控制 challenge、幂等缓存、SILENT */
void phone_session_process_timeouts(phone_session_t *sess);

// =============================================================================
// 毫秒时间工具
// =============================================================================

/** 获取当前单调毫秒时间 */
uint64_t phone_session_now_ms(void);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_SESSION_H */
