/***************************************************************************//**
 * @file phone_cfg.h
 * @brief APP-BG24 蓝牙通讯协议 V1.1 全部常量定义
 *
 * 本文件是 V1.1 协议的唯一常量来源。APP、BG24 固件和测试用例均以此为准。
 ******************************************************************************/
#ifndef PHONE_CFG_H
#define PHONE_CFG_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// =============================================================================
// 帧层常量 (第 5.1~5.2 节)
// =============================================================================

#define PHONE_FRAME_MAGIC              0xA5U   /* 帧起始标识 */
#define PHONE_FRAME_VERSION            0x11U   /* 协议 V1.1 */
#define PHONE_FRAME_HEADER_LEN         11U     /* 帧头固定长度 */
#define PHONE_FRAME_CRC_LEN            2U      /* CRC16 长度 */
#define PHONE_FRAME_MIN_LEN            (PHONE_FRAME_HEADER_LEN + PHONE_FRAME_CRC_LEN)  /* 13 */

/* MsgType */
#define PHONE_MSGTYPE_REQUEST          0x01U
#define PHONE_MSGTYPE_RESPONSE         0x02U
#define PHONE_MSGTYPE_EVENT            0x03U

/* FragIndex / FragTotal — V1.1 固定单帧 */
#define PHONE_FRAG_INDEX               0U
#define PHONE_FRAG_TOTAL               1U

// =============================================================================
// Flags 位定义 (第 5.2 节)
// =============================================================================

#define PHONE_FLAG_NEED_RSP            (1U << 0)  /* bit0: 需要业务响应 */
#define PHONE_FLAG_LAST_FRAG           (1U << 1)  /* bit1: 最后分片 (加密帧固定1) */
#define PHONE_FLAG_HAS_AUTH_TAG        (1U << 2)  /* bit2: 有 Auth Tag (加密帧固定1) */
#define PHONE_FLAG_ENCRYPTED           (1U << 3)  /* bit3: 加密帧 (加密帧固定1) */

/* 明文帧常用 Flags */
#define PHONE_FLAGS_PLAIN_RSP          (PHONE_FLAG_LAST_FRAG)
#define PHONE_FLAGS_PLAIN_NO_RSP       (PHONE_FLAG_LAST_FRAG)
/* 加密帧 Flags */
#define PHONE_FLAGS_ENCRYPTED_RSP      (PHONE_FLAG_LAST_FRAG | PHONE_FLAG_HAS_AUTH_TAG | PHONE_FLAG_ENCRYPTED)
#define PHONE_FLAGS_ENCRYPTED_NO_RSP   (PHONE_FLAG_LAST_FRAG | PHONE_FLAG_HAS_AUTH_TAG | PHONE_FLAG_ENCRYPTED)
/* Event Flags */
#define PHONE_FLAGS_EVENT              (PHONE_FLAG_LAST_FRAG | PHONE_FLAG_HAS_AUTH_TAG | PHONE_FLAG_ENCRYPTED)

// =============================================================================
// 命令码 (第 11 / 22.3 节)
// =============================================================================

#define PHONE_CMD_GET_DEVICE_INFO      0x01U
#define PHONE_CMD_GET_STATUS           0x03U
#define PHONE_CMD_BIND_HELLO           0x0FU
#define PHONE_CMD_QR_VERIFY            0x10U
#define PHONE_CMD_BIND_WINDOW_QUERY    0x11U
#define PHONE_CMD_REGISTER_APP_KEY     0x12U
#define PHONE_CMD_AUTH_CHALLENGE_REQ   0x20U
#define PHONE_CMD_AUTH_CHALLENGE_RSP   0x21U
#define PHONE_CMD_REBIND_REQUEST       0x30U
#define PHONE_CMD_REBIND_REGISTER_KEY  0x31U
#define PHONE_CMD_CTRL_CHALLENGE_REQ   0x40U
#define PHONE_CMD_CTRL_COMMAND         0x41U
#define PHONE_CMD_STATE_CHANGED_EVENT  0x50U

/* V1.2 新增: 无感蓝牙钥匙 PASSIVE 命令 (第 23 节) */
#define PHONE_CMD_PASSIVE_ENABLE           0x60U
#define PHONE_CMD_PASSIVE_DISABLE          0x61U
#define PHONE_CMD_PASSIVE_PAIR_PREPARE     0x62U
#define PHONE_CMD_PASSIVE_PAIR_READY       0x63U
#define PHONE_CMD_PASSIVE_PAIR_CANCEL      0x64U
#define PHONE_CMD_PASSIVE_SENSITIVITY_SET  0x65U
#define PHONE_CMD_PASSIVE_QUOTA_REFRESH    0x66U

// =============================================================================
// TLV Type 定义 (第 7 节)
// =============================================================================

#define PHONE_TLV_TOKEN_BODY           0x01U  /* UTF-8, max 320 */
#define PHONE_TLV_SIG                  0x02U  /* Binary 64 */
#define PHONE_TLV_DEVICE_ID            0x03U  /* UTF-8, max 32 */
#define PHONE_TLV_APP_PUBLIC_KEY       0x04U  /* Binary 65 */
#define PHONE_TLV_NONCE                0x05U  /* Binary 16 */
#define PHONE_TLV_APP_SIGNATURE        0x06U  /* Binary 64 */
#define PHONE_TLV_CMD_PARAM            0x07U  /* Binary, max 64 */
#define PHONE_TLV_APP_KEY_ID           0x08U  /* Binary 16 */
#define PHONE_TLV_AUTH_SESSION_ID      0x09U  /* Binary 16 */
#define PHONE_TLV_APP_COUNTER          0x0AU  /* U64 BE */
#define PHONE_TLV_BIND_STATE           0x0BU  /* U8 */
#define PHONE_TLV_PROTOCOL_VERSION     0x0DU  /* U8 */
#define PHONE_TLV_REMAIN_SEC           0x0EU  /* U16 BE */
#define PHONE_TLV_ERROR_CODE           0x0FU  /* U16 BE */
#define PHONE_TLV_RESULT               0x10U  /* U8 */
#define PHONE_TLV_QID                  0x11U  /* UTF-8, max 32 */
#define PHONE_TLV_CV                   0x12U  /* U32 BE */
#define PHONE_TLV_CONTROL_CMD          0x13U  /* U8 */
#define PHONE_TLV_CAPABILITY_FLAGS     0x15U  /* U32 BE */
#define PHONE_TLV_BIND_VERSION         0x16U  /* U32 BE */
#define PHONE_TLV_CHALLENGE_ID         0x18U  /* Binary 16 */
#define PHONE_TLV_VEHICLE_LOCK_STATE   0x19U  /* U8 */
#define PHONE_TLV_APP_ECDH_PUBLIC_KEY  0x1AU  /* Binary 65 */
#define PHONE_TLV_BG24_ECDH_PUBLIC_KEY 0x1BU  /* Binary 65 */
#define PHONE_TLV_PUBLIC_KEY_ALG       0x1CU  /* U8 */
#define PHONE_TLV_CONTROL_FLAGS        0x1DU  /* U8 */
#define PHONE_TLV_APP_BIND_NONCE       0x1EU  /* Binary 16 */
#define PHONE_TLV_BG_BIND_NONCE        0x1FU  /* Binary 16 */
#define PHONE_TLV_BIND_SESSION_ID      0x20U  /* Binary 16 */
#define PHONE_TLV_FW_VERSION           0x21U  /* 3 Byte */
#define PHONE_TLV_REQUEST_SEQ          0x22U  /* U16 BE */

/* V1.2 新增 TLV (第 7 / 23 节) */
#define PHONE_TLV_IGNITION_STATE       0x23U  /* U8 */
#define PHONE_TLV_REMAINING_RANGE      0x24U  /* U16 BE */
#define PHONE_TLV_DOOR_STATE           0x25U  /* U8 bitmask */
#define PHONE_TLV_PAIRING_WINDOW_ID    0x26U  /* Binary 8 */
#define PHONE_TLV_PAIRING_PASSKEY      0x27U  /* U32 BE */
#define PHONE_TLV_PASSIVE_SENSITIVITY  0x28U  /* U8 */

/* TLV 长度常量 */
#define PHONE_TLV_LEN_SIG              64U
#define PHONE_TLV_LEN_PUBKEY           65U
#define PHONE_TLV_LEN_NONCE            16U
#define PHONE_TLV_LEN_KEY_ID           16U
#define PHONE_TLV_LEN_SESSION_ID       16U
#define PHONE_TLV_LEN_CHALLENGE_ID     16U
#define PHONE_TLV_LEN_APP_COUNTER      8U
#define PHONE_TLV_LEN_BIND_VERSION     4U
#define PHONE_TLV_LEN_CAPABILITY_FLAGS 4U
#define PHONE_TLV_LEN_CV               4U
#define PHONE_TLV_LEN_FW_VERSION       3U
#define PHONE_TLV_LEN_PAIRING_WINDOW_ID 8U
#define PHONE_TLV_LEN_PAIRING_PASSKEY   4U
#define PHONE_TLV_LEN_U8               1U
#define PHONE_TLV_LEN_U16              2U
#define PHONE_TLV_LEN_U32              4U

/* TLV 最大值 */
#define PHONE_TLV_MAX_TOKEN_BODY       320U
#define PHONE_TLV_MAX_DEVICE_ID        32U
#define PHONE_TLV_MAX_QID              32U
#define PHONE_TLV_MAX_CMD_PARAM        64U

// =============================================================================
// 抽象错误码 (第 14 节)
// =============================================================================

#define PHONE_ERR_OK                   0x0000U
#define PHONE_ERR_PARAM_INVALID        0x0001U
#define PHONE_ERR_STATE_NOT_ALLOWED    0x0002U
#define PHONE_ERR_AUTH_FAILED          0x0003U
#define PHONE_ERR_AUTH_COND_NOT_MET    0x0004U
#define PHONE_ERR_TIMEOUT              0x0005U
#define PHONE_ERR_BUSY                 0x0006U
#define PHONE_ERR_DEVICE_SILENT        0x0007U
#define PHONE_ERR_DEVICE_LOCKED        0x0008U
#define PHONE_ERR_UNSUPPORTED_VERSION  0x0009U
#define PHONE_ERR_INTERNAL_ERROR       0x000AU

/* V1.2 新增错误码 (第 14 / 23 节) */
#define PHONE_ERR_VEHICLE_ASSOCIATION_FAILED  0x000BU  /* VIN 车辆关联校验失败 */
#define PHONE_ERR_PAIRING_REQUIRED            0x000CU  /* 无有效 Bond, 需 Pairing */
#define PHONE_ERR_PAIRING_WINDOW_INVALID      0x000DU  /* Pairing 上下文无效 */

// =============================================================================
// Result 值
// =============================================================================

#define PHONE_RESULT_OK                0x00U
#define PHONE_RESULT_FAIL              0x01U

// =============================================================================
// 设备状态 (bindState / deviceState, 第 3 / 22 节)
// =============================================================================

#define PHONE_DEVICE_STATE_UNKNOWN          0x00U
#define PHONE_DEVICE_STATE_UNBOUND          0x01U
#define PHONE_DEVICE_STATE_BOUND            0x02U
#define PHONE_DEVICE_STATE_REBIND_WINDOW    0x03U
#define PHONE_DEVICE_STATE_SILENT           0x04U
#define PHONE_DEVICE_STATE_SECURITY_LOCKED  0x05U
#define PHONE_DEVICE_STATE_SERVICE_MODE     0x06U
#define PHONE_DEVICE_STATE_ERROR            0x07U

// =============================================================================
// vehicleLockState (第 13 节)
// =============================================================================

#define PHONE_LOCK_STATE_UNKNOWN     0x00U
#define PHONE_LOCK_STATE_LOCKED      0x01U
#define PHONE_LOCK_STATE_UNLOCKED    0x02U

// =============================================================================
// controlCmd (第 7 节 TLV 0x13)
// =============================================================================

#define PHONE_CONTROL_CMD_UNLOCK     0x01U
#define PHONE_CONTROL_CMD_LOCK       0x02U
#define PHONE_CONTROL_CMD_FIND_CAR   0x03U

// =============================================================================
// ignitionState (第 13 节, V1.2 新增 TLV 0x23)
// =============================================================================

#define PHONE_IGNITION_STATE_UNKNOWN  0x00U
#define PHONE_IGNITION_STATE_OFF      0x01U
#define PHONE_IGNITION_STATE_ACC      0x02U
#define PHONE_IGNITION_STATE_ON       0x03U
#define PHONE_IGNITION_STATE_START    0x04U

// =============================================================================
// publicKeyAlg (第 7 节 TLV 0x1C)
// =============================================================================

#define PHONE_PUBKEY_ALG_ECDSA_P256_SHA256  0x01U

// =============================================================================
// doorState (第 13 节, V1.2 新增 TLV 0x25)
// =============================================================================

#define PHONE_DOOR_STATE_ALL_CLOSED   0x00U
#define PHONE_DOOR_STATE_LEFT_OPEN    0x01U
#define PHONE_DOOR_STATE_RIGHT_OPEN   0x02U
#define PHONE_DOOR_STATE_BOTH_OPEN    0x03U
#define PHONE_DOOR_STATE_UNKNOWN      0xFFU

// =============================================================================
// capabilityFlags 位定义 (第 7 节)
// =============================================================================

#define PHONE_CAP_AES_CCM_SESSION     (1U << 0)  /* bit0: 支持 AES-CCM 业务会话 */
#define PHONE_CAP_VEHICLE_LOCK_STATE  (1U << 1)  /* bit1: 支持 vehicleLockState */
#define PHONE_CAP_STATE_CHANGED_EVENT (1U << 2)  /* bit2: 支持 STATE_CHANGED_EVENT */
#define PHONE_CAP_REBIND              (1U << 3)  /* bit3: 支持换绑 */
#define PHONE_CAP_PRIVATE_QR          (1U << 4)  /* bit4: 支持私有加密二维码 */
#define PHONE_CAP_PASSIVE_KEY         (1U << 5)  /* bit5: CR-008 无感蓝牙钥匙 (V1.2) */

/* V1.2 默认能力: bit0~bit5 全部支持 */
#define PHONE_CAP_DEFAULT_V11         0x0000003FU

// =============================================================================
// passiveSensitivity (第 23.8 节, V1.2 新增 TLV 0x28)
// =============================================================================

#define PHONE_PASSIVE_SENS_NEAR       0x01U
#define PHONE_PASSIVE_SENS_STANDARD   0x02U
#define PHONE_PASSIVE_SENS_FAR        0x03U

// =============================================================================
// 超时与重试 (第 16 / 22.9 节)
// =============================================================================

#define PHONE_TIMEOUT_BLE_CONNECT_MS        10000U  /* BLE 连接 */
#define PHONE_TIMEOUT_SERVICE_DISCOVERY_MS   5000U  /* 服务发现 */
#define PHONE_TIMEOUT_NOTIFY_ENABLE_MS       3000U  /* Notify 使能 */
#define PHONE_TIMEOUT_GET_DEVICE_INFO_MS     3000U  /* GET_DEVICE_INFO */
#define PHONE_TIMEOUT_GENERIC_CMD_MS         3000U  /* 普通命令响应 */
#define PHONE_TIMEOUT_BIND_HELLO_MS          5000U  /* BIND_HELLO */
#define PHONE_TIMEOUT_QR_VERIFY_MS           5000U  /* QR_VERIFY */
#define PHONE_TIMEOUT_BIND_WINDOW_MS        60000U  /* 绑定/换绑窗口 */
#define PHONE_TIMEOUT_AUTH_CHALLENGE_MS     10000U  /* AUTH challenge (协议 §22.9: 10 秒) */
#define PHONE_TIMEOUT_CTRL_CHALLENGE_MS     10000U  /* 控制 challenge */
#define PHONE_TIMEOUT_CTRL_COMMAND_MS        5000U  /* CTRL_COMMAND 最终结果 */
#define PHONE_TIMEOUT_FRAG_REASSEMBLE_MS     2000U  /* 分包组包 (V1.1 保留) */
#define PHONE_TIMEOUT_IDEMPOTENT_CACHE_MS   10000U  /* 幂等响应缓存 */

/* V1.2 新增: PASSIVE 系列超时 (第 23.12 节) */
#define PHONE_TIMEOUT_PASSIVE_CMD_MS         3000U  /* PASSIVE_* 普通命令响应 */
#define PHONE_TIMEOUT_PAIR_PREPARE_MS       30000U  /* PREPARE 准备期 */
#define PHONE_TIMEOUT_PAIR_READY_MS         60000U  /* READY 系统 Pairing */

// =============================================================================
// MTU 阈值 (第 6 / 22.1 节)
// =============================================================================

#define PHONE_MTU_TARGET                247U  /* 目标 MTU */
#define PHONE_MTU_MIN_BIND              219U  /* 绑定/换绑最低 MTU */
#define PHONE_MTU_MIN_CONTROL           194U  /* 控制/状态最低 MTU */
#define PHONE_MTU_MAX_FRAME_PAYLOAD     236U  /* 247 - header(11) */

// =============================================================================
// 加密帧常量
// =============================================================================

#define PHONE_SECURITY_COUNTER_LEN      8U    /* U64 BE */
#define PHONE_AES_CCM_KEY_LEN          16U    /* AES-128 */
#define PHONE_AES_CCM_TAG_LEN           8U    /* CCM Tag */
#define PHONE_AES_CCM_NONCE_LEN        13U    /* direction(1) + sessionShort(4) + counter(8) */
#define PHONE_AES_CCM_DIR_APP_TO_BG24  0xA1U  /* APP → BG24 */
#define PHONE_AES_CCM_DIR_BG24_TO_APP  0xB1U  /* BG24 → APP */

/* 加密帧 overhead = SecurityCounter(8) + Tag(8) + CRC(2) */
#define PHONE_ENCRYPTED_OVERHEAD       (PHONE_SECURITY_COUNTER_LEN + PHONE_AES_CCM_TAG_LEN + PHONE_FRAME_CRC_LEN)

// =============================================================================
// 密码学常量
// =============================================================================

#define PHONE_ECDSA_P256_PUBKEY_LEN    65U    /* 04 || X || Y */
#define PHONE_ECDSA_P256_SIG_LEN       64U    /* r || s, BE */
#define PHONE_ECDH_SHARED_SECRET_LEN   32U    /* P-256 shared secret */
#define PHONE_SHA256_LEN               32U
#define PHONE_SESSION_KEY_LEN          16U    /* AES-128 */
#define PHONE_BIND_SECRET_LEN          16U

/* HKDF Info 前缀 (第 9.1 / 10 / 22.1 节) */
#define PHONE_HKDF_INFO_BIND           "BLEKEY-BIND-V1"
#define PHONE_HKDF_INFO_BIND_LEN       14U
#define PHONE_HKDF_INFO_SESSION        "BLEKEY-AES-CCM-V1"
#define PHONE_HKDF_INFO_SESSION_LEN    17U  /* "BLEKEY-AES-CCM-V1" = 17 chars, 不含 null */

/* ECDSA 签名原文前缀 (第 10.1~10.2 节) */
#define PHONE_SIGN_PREFIX_AUTH         "BLEKEY-AUTH-V1"
#define PHONE_SIGN_PREFIX_AUTH_LEN     14U
#define PHONE_SIGN_PREFIX_CTRL         "BLEKEY-CTRL-V1"
#define PHONE_SIGN_PREFIX_CTRL_LEN     14U

/* 二维码 AAD 前缀 (第 8 节) */
#define PHONE_QR_AAD                   "QK\x11"  /* QK + qrVersion=11 */
#define PHONE_QR_AAD_LEN               3U

// =============================================================================
// SILENT / 安全锁定 (第 14 节)
// =============================================================================

#define PHONE_SILENT_MAX_FAIL_COUNT    3U      /* 连续安全失败进入 SILENT */
#define PHONE_SILENT_DEFAULT_SEC      60U      /* 默认静默时长 */

/* V1.2 新增: Pairing/Bond 常量 (第 23 节) */
#define PHONE_PAIRING_PASSKEY_MAX         999999U  /* 6位Passkey最大值 */
#define PHONE_PAIRING_WINDOW_ID_LEN           8U
#define PHONE_PAIRING_MAX_FAIL_PER_SESSION     3U  /* 单AUTH session最多连续失败Pairing次数 */
#define PHONE_PASSIVE_QUOTA_DEFAULT             5U  /* 自动解锁默认额度 (QUOTA_REFRESH 重置为此值) */

/* STATE_CHANGED_EVENT 状态稳定延迟 (协议 §13: 100~300ms) */
#define PHONE_LOCK_STATE_DEBOUNCE_MS  200U     /* 状态稳定后 200ms 发送事件 */

// =============================================================================
// 广播参数 (第 3 / 22.7 节)
// =============================================================================

#define PHONE_ADV_MAGIC_B1             0xB1U   /* 厂商数据 magic 第 1 字节 */
#define PHONE_ADV_MAGIC_B2             0x24U   /* 厂商数据 magic 第 2 字节 (B1 24) */
#define PHONE_ADV_VERSION              0x11U   /* 广播版本 = V1.1 */
#define PHONE_ADV_COMPANY_ID           0x1234U /* 开发联调 Company ID (量产前替换) */

/* 广播间隔 (单位 0.625ms) */
#define PHONE_ADV_INTERVAL_UNBOUND_MIN  240U   /* 150ms / 0.625 */
#define PHONE_ADV_INTERVAL_UNBOUND_MAX  240U
#define PHONE_ADV_INTERVAL_BOUND_MIN    640U   /* 400ms / 0.625 */
#define PHONE_ADV_INTERVAL_BOUND_MAX    640U
#define PHONE_ADV_INTERVAL_SILENT_MIN  1600U   /* 1000ms / 0.625 */
#define PHONE_ADV_INTERVAL_SILENT_MAX  1600U

/* 广播名称前缀 */
#define PHONE_ADV_NAME_PREFIX           "BLEKEY_"

/* 广播 Manufacturer Data 长度: CompanyId(2) + B1(1) + 24(1) + advVersion(1) + shortDid(2) + deviceState(1) + flags(1) = 9 */
#define PHONE_ADV_MANUF_DATA_LEN        9U

// =============================================================================
// 设备 ID
// =============================================================================

#define PHONE_DEVICE_ID_MAX_LEN         32U

/* 固件版本 */
#define PHONE_FW_VERSION_MAJOR          1U
#define PHONE_FW_VERSION_MINOR          2U
#define PHONE_FW_VERSION_PATCH          0U

// =============================================================================
// 最大缓冲区
// =============================================================================

/* 完整加密帧最大长度: Header(11) + SecurityCounter(8) + Ciphertext(236) + Tag(8) + CRC(2) */
#define PHONE_MAX_ENCRYPTED_FRAME_LEN  265U
/* 完整明文帧最大长度: Header(11) + Payload(236) + CRC(2) */
#define PHONE_MAX_PLAIN_FRAME_LEN      249U
/* 最大 Payload 长度 */
#define PHONE_MAX_PAYLOAD_LEN          PHONE_MTU_MAX_FRAME_PAYLOAD

// =============================================================================
// 广播刷新与恢复
// =============================================================================

#define PHONE_LINK_ADV_DEFAULT_REFRESH_MS     10000U   /* 广播数据刷新周期 */
#define PHONE_LINK_SECONDARY_ADV_TIMEOUT_MS    60000U   /* 副广播超时恢复 */

/* ========================================================================== */
/* 调试开关: 帧 hex dump 打印 (阻塞串口 300-450ms/帧, 默认关闭)                 */
/* 调试时取消下面注释即可恢复 hex dump                                            */
/* ========================================================================== */
#define PHONE_DUMP_FRAMES  1

#ifdef __cplusplus
}
#endif

#endif /* PHONE_CFG_H */
