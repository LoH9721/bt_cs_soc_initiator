/***************************************************************************//**
 * @file phone_sm.c
 * @brief V1.1 协议状态机完整实现。
 *
 * 接收流水线:
 *   Frame → CRC → Idempotent → Counter → CCM → TLV → Validate → Execute
 *
 * 命令处理:
 *   0x01 GET_DEVICE_INFO     0x0F BIND_HELLO
 *   0x10 QR_VERIFY           0x11 BIND_WINDOW_QUERY
 *   0x12 REGISTER_APP_KEY    0x20 AUTH_CHALLENGE_REQ
 *   0x21 AUTH_CHALLENGE_RSP  0x30 REBIND_REQUEST
 *   0x31 REBIND_REGISTER_KEY 0x03 GET_STATUS
 *   0x40 CTRL_CHALLENGE_REQ  0x41 CTRL_COMMAND
 *   0x50 STATE_CHANGED_EVENT (主动发送)
 ******************************************************************************/

#include "user_phone/data/phone_sm.h"
#include "user_phone/data/phone_frame.h"
#include "user_phone/data/phone_tlv.h"
#include "user_phone/data/phone_crypto.h"
#include "user_phone/data/phone_session.h"
#include "user_phone/data/phone_storage.h"
#include "user_phone/phone_cfg.h"
#include "user_log_console.h"
#include "user_can_common/CanManage/CanManage.h"
#include "user_vin.h"
#include "user_vehicle_state.h"
#include "user_hid/hid_service.h"
#include "sl_bt_api.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/* 内部: 命令名                                                                */
/* (调试开关 PHONE_DUMP_FRAMES 见 phone_cfg.h)                                  */
/* ========================================================================== */

static const char *cmd_name(uint8_t cmd)
{
  switch (cmd) {
    case 0x01: return "GET_DEVICE_INFO";
    case 0x03: return "GET_STATUS";
    case 0x0F: return "BIND_HELLO";
    case 0x10: return "QR_VERIFY";
    case 0x11: return "BIND_WINDOW_QUERY";
    case 0x12: return "REGISTER_APP_KEY";
    case 0x20: return "AUTH_CHALLENGE_REQ";
    case 0x21: return "AUTH_CHALLENGE_RSP";
    case 0x30: return "REBIND_REQUEST";
    case 0x31: return "REBIND_REGISTER_KEY";
    case 0x40: return "CTRL_CHALLENGE_REQ";
    case 0x41: return "CTRL_COMMAND";
    case 0x50: return "STATE_CHANGED_EVENT";
    /* V1.2 新增: 无感蓝牙钥匙 */
    case 0x60: return "PASSIVE_ENABLE";
    case 0x61: return "PASSIVE_DISABLE";
    case 0x62: return "PASSIVE_PAIR_PREPARE";
    case 0x63: return "PASSIVE_PAIR_READY";
    case 0x64: return "PASSIVE_PAIR_CANCEL";
    case 0x65: return "PASSIVE_SENSITIVITY_SET";
    case 0x66: return "PASSIVE_QUOTA_REFRESH";
    default:   return "UNKNOWN";
  }
}

static const char *msg_type_str(uint8_t mt)
{
  switch (mt) {
    case 0x01: return "REQ";
    case 0x02: return "RSP";
    case 0x03: return "EVT";
    default:   return "?";
  }
}

static const char *msg_type_cn(uint8_t mt)
{
  switch (mt) {
    case 0x01: return "请求";
    case 0x02: return "响应";
    case 0x03: return "事件";
    default:   return "?";
  }
}

/* ========================================================================== */
/* 内部: 协议数据可视化打印 (Pretty Protocol Dump)                              */
/* ========================================================================== */

/** 将字节数组格式化为完整 hex 字符串 (不截断, 双缓冲交替) */
static const char *hex_str_full(const uint8_t *data, uint16_t len)
{
  static char buf[2][768];  /* 双缓冲, 同一日志行可调用2次不覆盖 */
  static int idx = 0;
  char *b = buf[(idx++) & 1];
  uint16_t i, pos = 0U;
  uint16_t limit = (len < 255U) ? len : 255U;
  b[0] = '\0';  /* 0 字节输入时返回空串, 避免残留脏数据 */
  for (i = 0U; i < limit && pos < 765U; i++) {
    pos += (uint16_t)snprintf(&b[pos], 768U - pos, "%02X ", (unsigned)data[i]);
  }
  if (len > 255U) {
    pos += (uint16_t)snprintf(&b[pos], 768U - pos, "...(%u bytes total)", (unsigned)len);
  }
  return b;
}

/** 将短字节数组格式化为 hex (不换行, 最多64字节, 双缓冲交替) */
static const char *hex_str(const uint8_t *data, uint16_t len, uint16_t dump_len)
{
  static char buf[2][192];
  static int idx = 0;
  char *b = buf[(idx++) & 1];
  uint16_t i, limit = (len < dump_len) ? len : dump_len;
  uint16_t pos = 0U;
  for (i = 0U; i < limit && pos < 190U; i++) {
    pos += (uint16_t)snprintf(&b[pos], 192U - pos, "%02X ", (unsigned)data[i]);
  }
  if (len > dump_len) {
    pos += (uint16_t)snprintf(&b[pos], 192U - pos, "...");
  }
  return b;
}

/* ARM 32-bit newlib-nano printf 有 %llX/%llu 的 bug,
 * 用纯手动格式化避开所有 64-bit 格式化问题 */

/** uint64_t → hex 字符串 (双缓冲交替, 16 进制) */
static const char *u64_hex_str(uint64_t val)
{
  static char buf[2][19];  /* "0x" + 16 nibbles + null = 19 */
  static int idx = 0;
  char *b = buf[(idx++) & 1];
  static const char hex[] = "0123456789ABCDEF";
  int i;
  b[0] = '0';
  b[1] = 'x';
  for (i = 0; i < 16; i++) {
    b[2 + i] = hex[(val >> (60U - 4U * (unsigned)i)) & 0xFU];
  }
  b[18] = '\0';
  return b;
}

/** uint64_t → 十进制字符串 (双缓冲交替) */
static const char *u64_dec_str(uint64_t val)
{
  static char buf[2][21];  /* max U64 = 20 digits + null */
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

/** TLV Type → Emoji */
static const char *tlv_emoji(uint8_t type)
{
  return " ";
  // switch (type) {
  //   case PHONE_TLV_RESULT:                return "🎯";
  //   case PHONE_TLV_ERROR_CODE:            return "❌";
  //   case PHONE_TLV_DEVICE_ID:             return "🆔";
  //   case PHONE_TLV_PROTOCOL_VERSION:      return "📌";
  //   case PHONE_TLV_FW_VERSION:            return "📝";
  //   case PHONE_TLV_BIND_STATE:            return "🔗";
  //   case PHONE_TLV_CAPABILITY_FLAGS:      return "💪";
  //   case PHONE_TLV_TOKEN_BODY:            return "📦";
  //   case PHONE_TLV_SIG:                   return "✍️";
  //   case PHONE_TLV_APP_PUBLIC_KEY:        return "🔑";
  //   case PHONE_TLV_NONCE:                 return "🎲";
  //   case PHONE_TLV_APP_SIGNATURE:         return "✍️";
  //   case PHONE_TLV_CMD_PARAM:             return "⚙️";
  //   case PHONE_TLV_APP_KEY_ID:            return "🏷️";
  //   case PHONE_TLV_AUTH_SESSION_ID:       return "🔐";
  //   case PHONE_TLV_APP_COUNTER:           return "🔢";
  //   case PHONE_TLV_REMAIN_SEC:            return "⏱️";
  //   case PHONE_TLV_QID:                   return "📋";
  //   case PHONE_TLV_CV:                    return "🔢";
  //   case PHONE_TLV_CONTROL_CMD:           return "🎮";
  //   case PHONE_TLV_BIND_VERSION:          return "#️⃣";
  //   case PHONE_TLV_CHALLENGE_ID:          return "🎲";
  //   case PHONE_TLV_VEHICLE_LOCK_STATE:    return "🚗";
  //   case PHONE_TLV_APP_ECDH_PUBLIC_KEY:   return "🔑";
  //   case PHONE_TLV_BG24_ECDH_PUBLIC_KEY:  return "🔑";
  //   case PHONE_TLV_PUBLIC_KEY_ALG:        return "🏷️";
  //   case PHONE_TLV_CONTROL_FLAGS:         return "🎛️";
  //   case PHONE_TLV_APP_BIND_NONCE:        return "🎲";
  //   case PHONE_TLV_BG_BIND_NONCE:         return "🎲";
  //   case PHONE_TLV_BIND_SESSION_ID:       return "🔐";
  //   case PHONE_TLV_REQUEST_SEQ:           return "🔢";
  //   default:                              return "❓";
  // }
}

/** TLV Type → 中文名(英文名) */
static const char *tlv_label(uint8_t type)
{
  switch (type) {
    case PHONE_TLV_RESULT:                return "结果(result)";
    case PHONE_TLV_ERROR_CODE:            return "错误码(errorCode)";
    case PHONE_TLV_DEVICE_ID:             return "设备ID(deviceId)";
    case PHONE_TLV_PROTOCOL_VERSION:      return "协议版本(protocolVersion)";
    case PHONE_TLV_FW_VERSION:            return "固件版本(fwVersion)";
    case PHONE_TLV_BIND_STATE:            return "绑定状态(bindState)";
    case PHONE_TLV_CAPABILITY_FLAGS:      return "能力标志(capabilityFlags)";
    case PHONE_TLV_TOKEN_BODY:            return "令牌体(tokenBody)";
    case PHONE_TLV_SIG:                   return "厂家签名(sig)";
    case PHONE_TLV_APP_PUBLIC_KEY:        return "APP公钥(appPublicKey)";
    case PHONE_TLV_NONCE:                 return "随机数(nonce)";
    case PHONE_TLV_APP_SIGNATURE:         return "APP签名(appSignature)";
    case PHONE_TLV_CMD_PARAM:             return "命令参数(cmdParam)";
    case PHONE_TLV_APP_KEY_ID:            return "APP密钥ID(appKeyId)";
    case PHONE_TLV_AUTH_SESSION_ID:       return "认证会话ID(authSessionId)";
    case PHONE_TLV_APP_COUNTER:           return "APP计数器(appCounter)";
    case PHONE_TLV_REMAIN_SEC:            return "剩余秒数(remainSec)";
    case PHONE_TLV_QID:                   return "二维码ID(qid)";
    case PHONE_TLV_CV:                    return "验证码(cv)";
    case PHONE_TLV_CONTROL_CMD:           return "控制命令(controlCmd)";
    case PHONE_TLV_BIND_VERSION:          return "绑定版本(bindVersion)";
    case PHONE_TLV_CHALLENGE_ID:          return "挑战ID(challengeId)";
    case PHONE_TLV_VEHICLE_LOCK_STATE:    return "车锁状态(vehicleLockState)";
    case PHONE_TLV_APP_ECDH_PUBLIC_KEY:   return "APP ECDH公钥(appEcdhPublicKey)";
    case PHONE_TLV_BG24_ECDH_PUBLIC_KEY:  return "BG24 ECDH公钥(bg24EcdhPublicKey)";
    case PHONE_TLV_PUBLIC_KEY_ALG:        return "公钥算法(publicKeyAlg)";
    case PHONE_TLV_CONTROL_FLAGS:         return "控制标志(controlFlags)";
    case PHONE_TLV_APP_BIND_NONCE:        return "APP绑定随机数(appBindNonce)";
    case PHONE_TLV_BG_BIND_NONCE:         return "BG绑定随机数(bgBindNonce)";
    case PHONE_TLV_BIND_SESSION_ID:       return "绑定会话ID(bindSessionId)";
    case PHONE_TLV_REQUEST_SEQ:           return "请求序列号(requestSeq)";
    /* V1.2 新增 */
    case PHONE_TLV_IGNITION_STATE:        return "点火状态(ignitionState)";
    case PHONE_TLV_REMAINING_RANGE:       return "剩余续航(remainingRange)";
    case PHONE_TLV_DOOR_STATE:            return "门状态(doorState)";
    case PHONE_TLV_PAIRING_WINDOW_ID:     return "配对窗口ID(pairingWindowId)";
    case PHONE_TLV_PAIRING_PASSKEY:       return "配对密码(pairingPasskey)";
    case PHONE_TLV_PASSIVE_SENSITIVITY:   return "无感灵敏度(passiveSensitivity)";
    default:                              return "未知(UNKNOWN)";
  }
}

/** 格式化 TLV 值为可读字符串 (使用静态缓冲区, 单线程安全)
 *  格式: [RAW_BYTES] 语义值 — 例如 "[0x00] 🔓 已解锁(UNLOCKED)" */
static const char *tlv_value_str(uint8_t type, const uint8_t *value, uint16_t len)
{
  static char buf[128];
  char hex_prefix[40];  /* 如 "[0x%02X%02X... ]" */
  uint16_t hp = 0U;

  /* 先构建原始字节前缀 */
  if (len <= 8) {
    hp += (uint16_t)snprintf(hex_prefix, sizeof(hex_prefix), "[");
    for (uint16_t i = 0U; i < len; i++) {
      hp += (uint16_t)snprintf(&hex_prefix[hp], sizeof(hex_prefix) - hp,
                                "%02X", (unsigned)value[i]);
    }
    hp += (uint16_t)snprintf(&hex_prefix[hp], sizeof(hex_prefix) - hp, "]");
  } else {
    /* 长数据只显示前8字节 */
    hp += (uint16_t)snprintf(hex_prefix, sizeof(hex_prefix), "[");
    for (uint16_t i = 0U; i < 8U; i++) {
      hp += (uint16_t)snprintf(&hex_prefix[hp], sizeof(hex_prefix) - hp,
                                "%02X", (unsigned)value[i]);
    }
    hp += (uint16_t)snprintf(&hex_prefix[hp], sizeof(hex_prefix) - hp, "..]");
  }

  switch (type) {
    /* ---- 单字节语义值 ---- */
    case PHONE_TLV_RESULT:
      if (len == 1) {
        const char *s;
        switch (value[0]) {
          case PHONE_RESULT_OK:   s = "✅ 成功(OK)"; break;
          case PHONE_RESULT_FAIL: s = "❌ 失败(FAIL)"; break;
          default:                s = NULL; break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    case PHONE_TLV_BIND_STATE:
      if (len == 1) {
        const char *s;
        switch (value[0]) {
          case PHONE_DEVICE_STATE_UNKNOWN:        s = "❓ 未知(UNKNOWN)"; break;
          case PHONE_DEVICE_STATE_UNBOUND:        s = "🔓 未绑定(UNBOUND)"; break;
          case PHONE_DEVICE_STATE_BOUND:          s = "🔒 已绑定(BOUND)"; break;
          case PHONE_DEVICE_STATE_REBIND_WINDOW:  s = "🔄 换绑窗口(REBIND_WINDOW)"; break;
          case PHONE_DEVICE_STATE_SILENT:         s = "🔇 静默(SILENT)"; break;
          case PHONE_DEVICE_STATE_SECURITY_LOCKED:s = "🚫 安全锁定(SECURITY_LOCKED)"; break;
          case PHONE_DEVICE_STATE_SERVICE_MODE:   s = "🔧 服务模式(SERVICE_MODE)"; break;
          case PHONE_DEVICE_STATE_ERROR:          s = "💥 错误(ERROR)"; break;
          default:                                s = NULL; break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    case PHONE_TLV_VEHICLE_LOCK_STATE:
      if (len == 1) {
        const char *s;
        switch (value[0]) {
          case PHONE_LOCK_STATE_UNKNOWN:  s = "❓ 未知(UNKNOWN)"; break;
          case PHONE_LOCK_STATE_LOCKED:   s = "🔒 已上锁(LOCKED)"; break;
          case PHONE_LOCK_STATE_UNLOCKED: s = "🔓 已解锁(UNLOCKED)"; break;
          default:                        s = NULL; break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    case PHONE_TLV_CONTROL_CMD:
      if (len == 1) {
        const char *s;
        switch (value[0]) {
          case PHONE_CONTROL_CMD_UNLOCK:   s = "🔓 解锁(UNLOCK)"; break;
          case PHONE_CONTROL_CMD_LOCK:     s = "🔒 上锁(LOCK)"; break;
          case PHONE_CONTROL_CMD_FIND_CAR: s = "📍 寻车(FIND_CAR)"; break;
          default:                         s = NULL; break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    case PHONE_TLV_PUBLIC_KEY_ALG:
      if (len == 1) {
        const char *s = NULL;
        switch (value[0]) {
          case PHONE_PUBKEY_ALG_ECDSA_P256_SHA256: s = "ECDSA-P256-SHA256"; break;
          default: break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    /* ---- 多字节特殊格式 ---- */
    case PHONE_TLV_FW_VERSION:
      if (len == 3) {
        snprintf(buf, sizeof(buf), "[%02X %02X %02X] v%u.%u.%u",
                 (unsigned)value[0], (unsigned)value[1], (unsigned)value[2],
                 (unsigned)value[0], (unsigned)value[1], (unsigned)value[2]);
        return buf;
      }
      break;

    case PHONE_TLV_ERROR_CODE:
      if (len == 2) {
        uint16_t ec = ((uint16_t)value[0] << 8) | value[1];
        const char *ec_name;
        switch (ec) {
          case PHONE_ERR_OK:                  ec_name = "OK"; break;
          case PHONE_ERR_PARAM_INVALID:       ec_name = "参数无效(PARAM_INVALID)"; break;
          case PHONE_ERR_STATE_NOT_ALLOWED:   ec_name = "状态不允许(STATE_NOT_ALLOWED)"; break;
          case PHONE_ERR_AUTH_FAILED:         ec_name = "认证失败(AUTH_FAILED)"; break;
          case PHONE_ERR_AUTH_COND_NOT_MET:   ec_name = "认证条件不满足(AUTH_COND_NOT_MET)"; break;
          case PHONE_ERR_TIMEOUT:             ec_name = "超时(TIMEOUT)"; break;
          case PHONE_ERR_BUSY:                ec_name = "忙(BUSY)"; break;
          case PHONE_ERR_DEVICE_SILENT:       ec_name = "设备静默(DEVICE_SILENT)"; break;
          case PHONE_ERR_DEVICE_LOCKED:       ec_name = "设备锁定(DEVICE_LOCKED)"; break;
          case PHONE_ERR_UNSUPPORTED_VERSION: ec_name = "不支持的版本(UNSUPPORTED_VERSION)"; break;
          case PHONE_ERR_INTERNAL_ERROR:      ec_name = "内部错误(INTERNAL_ERROR)"; break;
          /* V1.2 新增 */
          case PHONE_ERR_VEHICLE_ASSOCIATION_FAILED: ec_name = "车辆关联失败(VEHICLE_ASSOCIATION_FAILED)"; break;
          case PHONE_ERR_PAIRING_REQUIRED:           ec_name = "需要配对(PAIRING_REQUIRED)"; break;
          case PHONE_ERR_PAIRING_WINDOW_INVALID:     ec_name = "配对窗口无效(PAIRING_WINDOW_INVALID)"; break;
          default:                            ec_name = ""; break;
        }
        snprintf(buf, sizeof(buf), "%s 0x%04X — %s", hex_prefix, (unsigned)ec, ec_name);
        return buf;
      }
      break;

    case PHONE_TLV_CAPABILITY_FLAGS:
      if (len == 4) {
        uint32_t caps = ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16)
                      | ((uint32_t)value[2] << 8) | value[3];
        snprintf(buf, sizeof(buf), "%s 0x%08lX [AES-CCM:%c 锁状态:%c 状态事件:%c 换绑:%c 私密QR:%c 无感:%c]",
                 hex_prefix, (unsigned long)caps,
                 (caps & PHONE_CAP_AES_CCM_SESSION)     ? 'Y' : 'N',
                 (caps & PHONE_CAP_VEHICLE_LOCK_STATE)  ? 'Y' : 'N',
                 (caps & PHONE_CAP_STATE_CHANGED_EVENT) ? 'Y' : 'N',
                 (caps & PHONE_CAP_REBIND)              ? 'Y' : 'N',
                 (caps & PHONE_CAP_PRIVATE_QR)          ? 'Y' : 'N',
                 (caps & PHONE_CAP_PASSIVE_KEY)         ? 'Y' : 'N');
        return buf;
      }
      break;

    /* ---- V1.2 新增: 点火状态 ---- */
    case PHONE_TLV_IGNITION_STATE:
      if (len == 1) {
        const char *s;
        switch (value[0]) {
          case 0x00: s = "❓ 未知(UNKNOWN)"; break;
          case PHONE_IGNITION_STATE_OFF:   s = "⭕ 熄火(OFF)"; break;
          case PHONE_IGNITION_STATE_ACC:   s = "🔌 附件(ACC)"; break;
          case PHONE_IGNITION_STATE_ON:    s = "🔥 点火(ON)"; break;
          case PHONE_IGNITION_STATE_START: s = "🚀 启动(START)"; break;
          default:                         s = NULL; break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    /* ---- V1.2 新增: 门状态 ---- */
    case PHONE_TLV_DOOR_STATE:
      if (len == 1) {
        const char *s;
        switch (value[0]) {
          case PHONE_DOOR_STATE_ALL_CLOSED: s = "🚪 左右门均关(ALL_CLOSED)"; break;
          case PHONE_DOOR_STATE_LEFT_OPEN:  s = "🚪 左门未关(LEFT_OPEN)"; break;
          case PHONE_DOOR_STATE_RIGHT_OPEN: s = "🚪 右门未关(RIGHT_OPEN)"; break;
          case PHONE_DOOR_STATE_BOTH_OPEN:  s = "🚪 左右门均未关(BOTH_OPEN)"; break;
          case PHONE_DOOR_STATE_UNKNOWN:    s = "❓ 未知(UNKNOWN)"; break;
          default:                          s = NULL; break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    /* ---- V1.2 新增: 无感灵敏度 ---- */
    case PHONE_TLV_PASSIVE_SENSITIVITY:
      if (len == 1) {
        const char *s;
        switch (value[0]) {
          case PHONE_PASSIVE_SENS_NEAR:     s = "📍 近(Near)"; break;
          case PHONE_PASSIVE_SENS_STANDARD: s = "📍 标准(Standard)"; break;
          case PHONE_PASSIVE_SENS_FAR:      s = "📍 远(Far)"; break;
          default:                          s = NULL; break;
        }
        if (s) { snprintf(buf, sizeof(buf), "%s %s", hex_prefix, s); return buf; }
      }
      break;

    default:
      break;
  }

  /* ---- 通用字节数组格式化 (hex_prefix + 常用数值解释) ---- */
  if (len == 1) {
    snprintf(buf, sizeof(buf), "%s 0x%02X (%u)", hex_prefix,
             (unsigned)value[0], (unsigned)value[0]);
  } else if (len == 2) {
    uint16_t v = ((uint16_t)value[0] << 8) | value[1];
    snprintf(buf, sizeof(buf), "%s 0x%04X (%u)", hex_prefix, (unsigned)v, (unsigned)v);
  } else if (len == 4) {
    uint32_t v = ((uint32_t)value[0] << 24) | ((uint32_t)value[1] << 16)
               | ((uint32_t)value[2] << 8) | value[3];
    snprintf(buf, sizeof(buf), "%s 0x%08lX (%lu)", hex_prefix, (unsigned long)v, (unsigned long)v);
  } else if (len == 8) {
    uint64_t v = ((uint64_t)value[0] << 56) | ((uint64_t)value[1] << 48)
               | ((uint64_t)value[2] << 40) | ((uint64_t)value[3] << 32)
               | ((uint64_t)value[4] << 24) | ((uint64_t)value[5] << 16)
               | ((uint64_t)value[6] << 8) | value[7];
    snprintf(buf, sizeof(buf), "%s %s (%s)", hex_prefix,
             u64_hex_str(v), u64_dec_str(v));
  } else if (len <= 32) {
    /* 可能是短字符串或短二进制 */
    uint16_t pos = 0U;
    bool is_printable = true;
    for (uint16_t i = 0U; i < len; i++) {
      if (value[i] < 0x20U || value[i] > 0x7EU) { is_printable = false; break; }
    }
    pos += (uint16_t)snprintf(buf, sizeof(buf), "%s ", hex_prefix);
    if (is_printable) {
      pos += (uint16_t)snprintf(&buf[pos], sizeof(buf) - pos, "\"");
      for (uint16_t i = 0U; i < len && pos < sizeof(buf) - 4U; i++) {
        pos += (uint16_t)snprintf(&buf[pos], sizeof(buf) - pos, "%c", (char)value[i]);
      }
      pos += (uint16_t)snprintf(&buf[pos], sizeof(buf) - pos, "\"");
    } else {
      for (uint16_t i = 0U; i < len && pos < sizeof(buf) - 5U; i++) {
        pos += (uint16_t)snprintf(&buf[pos], sizeof(buf) - pos, "%02X ", (unsigned)value[i]);
      }
    }
    return buf;
  } else {
    /* 长二进制 */
    uint16_t pos = 0U;
    pos += (uint16_t)snprintf(buf, sizeof(buf), "%s ", hex_prefix);
    for (uint16_t i = 0U; i < 16U && pos < sizeof(buf) - 5U; i++) {
      pos += (uint16_t)snprintf(&buf[pos], sizeof(buf) - pos, "%02X ", (unsigned)value[i]);
    }
    snprintf(&buf[pos], sizeof(buf) - pos, "... (%u bytes)", (unsigned)len);
    return buf;
  }

  return buf;
}

/**
 * @brief 打印 TLV 列表 (可视化格式, 用于 RX 和 TX)
 * @param tlvs      TLV 数组
 * @param count     TLV 数量
 * @param prefix    行前缀 (如 "│   " 或 "  ")
 */
static void dump_tlv_list(const phone_tlv_t *tlvs, uint8_t count, const char *prefix)
{
  for (uint8_t i = 0; i < count; i++) {
    const phone_tlv_t *t = &tlvs[i];
    USER_LOG_INFO("%s%s [0x%02X] %s: %s" USER_LOG_NL,
                  prefix, tlv_emoji(t->type),
                  (unsigned)t->type,
                  tlv_label(t->type),
                  tlv_value_str(t->type, t->value, t->len));
  }
}

/**
 * @brief 打印帧头各字段（从raw字节逐字段解析打印）
 * @param raw      完整帧原始缓冲区
 * @param frame    已解码的帧
 * @param is_enc   是否加密帧
 */
static void dump_frame_header(const uint8_t *raw, const phone_frame_t *frame, bool is_enc)
{
#ifndef PHONE_DUMP_FRAMES
  (void)raw; (void)frame; (void)is_enc;
  return;
#endif
  /* --- 固定帧头(11字节) --- */
  USER_LOG_INFO("│   📋 帧头(Header, 11字节):" USER_LOG_NL);
  USER_LOG_INFO("│     [0] Magic:         0x%02X           (固定 0xA5)" USER_LOG_NL, (unsigned)raw[0]);
  USER_LOG_INFO("│     [1] Version:       0x%02X           (协议 V1.%u)" USER_LOG_NL,
                (unsigned)raw[1], (unsigned)raw[1]);
  USER_LOG_INFO("│     [2] MsgType:       0x%02X           (%s)" USER_LOG_NL,
                (unsigned)raw[2], msg_type_cn(raw[2]));
  USER_LOG_INFO("│     [3] Cmd:           0x%02X           (%s)" USER_LOG_NL,
                (unsigned)raw[3], cmd_name(raw[3]));
  {
    uint16_t s = ((uint16_t)raw[4] << 8) | raw[5];
    USER_LOG_INFO("│     [4-5] Seq:         %04X           (U16 BE, %u)" USER_LOG_NL,
                  (unsigned)s, (unsigned)s);
  }
  {
    uint8_t f = raw[6];
    USER_LOG_INFO("│     [6] Flags:         0x%02X           (NEED_RSP:%c LAST:%c AUTH_TAG:%c ENCRYPTED:%c)" USER_LOG_NL,
                  (unsigned)f,
                  (f & PHONE_FLAG_NEED_RSP)     ? 'Y' : 'N',
                  (f & PHONE_FLAG_LAST_FRAG)    ? 'Y' : 'N',
                  (f & PHONE_FLAG_HAS_AUTH_TAG) ? 'Y' : 'N',
                  (f & PHONE_FLAG_ENCRYPTED)    ? 'Y' : 'N');
  }
  USER_LOG_INFO("│     [7] FragIndex:     0x%02X           (V1.1 固定 0)" USER_LOG_NL, (unsigned)raw[7]);
  USER_LOG_INFO("│     [8] FragTotal:     0x%02X           (V1.1 固定 1)" USER_LOG_NL, (unsigned)raw[8]);
  {
    uint16_t pl = ((uint16_t)raw[9] << 8) | raw[10];
    USER_LOG_INFO("│     [9-10] PayloadLen: %04X           (U16 BE, %u 字节)" USER_LOG_NL,
                  (unsigned)pl, (unsigned)pl);
  }

  /* --- 加密帧额外字段 --- */
  if (is_enc) {
    USER_LOG_INFO("│   🔐 加密区:" USER_LOG_NL);
    USER_LOG_INFO("│     [11-18] SecurityCounter: %02X %02X %02X %02X %02X %02X %02X %02X  (U64 BE)" USER_LOG_NL,
                  (unsigned)raw[11], (unsigned)raw[12], (unsigned)raw[13], (unsigned)raw[14],
                  (unsigned)raw[15], (unsigned)raw[16], (unsigned)raw[17], (unsigned)raw[18]);

    /* ciphertext 位置: offset 19, 长度 = frame->payload_len */
    /* auth_tag 位置: offset 19 + payload_len, 长度 = 8 */
    uint16_t ct_off = PHONE_FRAME_HEADER_LEN + PHONE_SECURITY_COUNTER_LEN;  /* 11+8=19 */
    uint16_t ct_len = frame->payload_len;
    uint16_t tag_off = ct_off + ct_len;
    USER_LOG_INFO("│     [%u-%u] Ciphertext:   %s" USER_LOG_NL,
                  (unsigned)ct_off, (unsigned)(tag_off - 1U),
                  hex_str_full(&raw[ct_off], ct_len));
    USER_LOG_INFO("│     [%u-%u] AuthTag:      %02X %02X %02X %02X %02X %02X %02X %02X  (8 字节)" USER_LOG_NL,
                  (unsigned)tag_off, (unsigned)(tag_off + 7U),
                  (unsigned)raw[tag_off], (unsigned)raw[tag_off+1],
                  (unsigned)raw[tag_off+2], (unsigned)raw[tag_off+3],
                  (unsigned)raw[tag_off+4], (unsigned)raw[tag_off+5],
                  (unsigned)raw[tag_off+6], (unsigned)raw[tag_off+7]);
  }

  /* --- CRC --- */
  {
    uint16_t crc_off = (uint16_t)(frame->total_len - PHONE_FRAME_CRC_LEN);
    USER_LOG_INFO("│     [%u-%u] CRC16:        %02X %02X        (recv=0x%04X calc=0x%04X)" USER_LOG_NL,
                  (unsigned)crc_off, (unsigned)(crc_off + 1U),
                  (unsigned)raw[crc_off], (unsigned)raw[crc_off+1],
                  (unsigned)frame->crc_recv, (unsigned)frame->crc_calc);
  }
}

/**
 * @brief 统一 RX 数据打印: 原始hex → 帧头信息 → TLV详情
 * @param data     原始帧数据
 * @param len      帧长度
 * @param frame    已解码的帧 (可为 NULL)
 * @param tlvs     TLV 数组 (可为 NULL, count=0 表示无TLV)
 * @param count    TLV 数量
 */
static void dump_rx_frame(const uint8_t *data, uint16_t len,
                          const phone_frame_t *frame,
                          const phone_tlv_t *tlvs, uint8_t count)
{
  uint8_t cmd = frame ? frame->cmd : 0U;
  uint16_t seq = frame ? frame->seq : 0U;
  uint8_t mt = frame ? frame->msg_type : 0U;

  /* 每帧一行: 收到的命令值 (始终打印, 不受 PHONE_DUMP_FRAMES 门控) */
  USER_LOG_INFO("📥 [RX] %s (0x%02X) %s | Seq=%u | Len=%u | %s" USER_LOG_NL,
                cmd_name(cmd), (unsigned)cmd, msg_type_cn(mt),
                (unsigned)seq, (unsigned)len,
                frame && frame->is_encrypted ? "🔐加密" : "📋明文");

#ifndef PHONE_DUMP_FRAMES
  (void)data; (void)tlvs; (void)count;
  return;
#endif
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("📦 RAW[%u]: %s" USER_LOG_NL,
                (unsigned)len, hex_str_full(data, len));
#endif

  /* 帧头逐字段解析 */
  if (frame != NULL) {
    dump_frame_header(data, frame, frame->is_encrypted);
    USER_LOG_INFO("──────────────────────────────────────────────" USER_LOG_NL);
  }

  if (count > 0U && tlvs != NULL) {
    dump_tlv_list(tlvs, count, "│   ");
    USER_LOG_INFO("──────────────────────────────────────────────" USER_LOG_NL);
  } else if (frame && frame->is_encrypted) {
    USER_LOG_INFO("│   🔐 (加密载荷, %u 字节密文)" USER_LOG_NL, (unsigned)frame->payload_len);
    USER_LOG_INFO("──────────────────────────────────────────────" USER_LOG_NL);
  }
}

/**
 * @brief 统一 TX 数据打印: 原始hex → 帧头信息 → TLV详情
 * @param buf      完整帧缓冲区
 * @param buf_len  帧长度
 * @param cmd      命令码
 * @param seq      序列号
 * @param mt       消息类型 (REQ/RSP/EVT)
 * @param tlvs     TLV 数组 (可为 NULL)
 * @param count    TLV 数量
 * @param is_enc   是否加密帧
 */
static void dump_tx_frame(const uint8_t *buf, uint16_t buf_len,
                          uint8_t cmd, uint16_t seq, uint8_t mt,
                          const phone_tlv_t *tlvs, uint8_t count,
                          bool is_enc)
{
  /* 每帧一行: 回复时组包的命令值 (始终打印, 不受 PHONE_DUMP_FRAMES 门控) */
  USER_LOG_INFO("📤 [TX] %s (0x%02X) %s | Seq=%u | Len=%u | %s" USER_LOG_NL,
                cmd_name(cmd), (unsigned)cmd, msg_type_cn(mt),
                (unsigned)seq, (unsigned)buf_len,
                is_enc ? "🔐加密" : "📋明文");

#ifndef PHONE_DUMP_FRAMES
  (void)buf; (void)tlvs; (void)count;
  return;
#endif
  /* 构建临时 frame_t 以复用 dump_frame_header */
  phone_frame_t tmp_frame;
  memset(&tmp_frame, 0, sizeof(tmp_frame));
  tmp_frame.cmd = cmd;
  tmp_frame.seq = seq;
  tmp_frame.msg_type = mt;
  tmp_frame.total_len = buf_len;
  tmp_frame.crc_recv = (uint16_t)(((uint16_t)buf[buf_len - 2U] << 8) | buf[buf_len - 1U]);
  tmp_frame.crc_calc = tmp_frame.crc_recv;  /* TX 方向 CRC 为已计算值 */
  if (is_enc) {
    tmp_frame.is_encrypted = true;
    /* payload_len 从帧头提取 (加密帧: payload_len = ciphertext长度) */
    tmp_frame.payload_len = ((uint16_t)buf[9] << 8) | buf[10];
  } else {
    tmp_frame.payload_len = (uint16_t)(buf_len - PHONE_FRAME_HEADER_LEN - PHONE_FRAME_CRC_LEN);
  }
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("📦 RAW[%u]: %s" USER_LOG_NL,
                (unsigned)buf_len, hex_str_full(buf, buf_len));
#endif

  /* 帧头逐字段解析 */
  dump_frame_header(buf, &tmp_frame, is_enc);
  USER_LOG_INFO("──────────────────────────────────────────────" USER_LOG_NL);

  if (count > 0U && tlvs != NULL) {
    dump_tlv_list(tlvs, count, "│   ");
    USER_LOG_INFO("──────────────────────────────────────────────" USER_LOG_NL);
  } else if (is_enc) {
    USER_LOG_INFO("│   🔐 (加密载荷)" USER_LOG_NL);
    USER_LOG_INFO("──────────────────────────────────────────────" USER_LOG_NL);
  }
}

/* ========================================================================== */
/* 静态状态                                                                    */
/* ========================================================================== */

static phone_session_t         g_sess;
static phone_sm_send_fn_t      g_send_fn;
static phone_sm_disconnect_fn_t g_disc_fn;
static bool                    g_notify_enabled;
static bool                    g_connected;
static bool                    g_cmd_ok;               /* 当前命令处理结果 (成功=TRUE) */
static bool                    g_cmd_deferred;         /* 当前命令走异步延迟处理 (ECDSA/SE 等) */
static uint8_t                 g_pending_control_cmd;  /* one-shot */
static uint8_t                 g_vehicle_lock_state;   /* 当前锁状态 */
static uint8_t                 g_vehicle_ignition_state = PHONE_IGNITION_STATE_UNKNOWN;
static uint16_t                g_vehicle_remaining_range = 0xFFFFU; /* UNKNOWN */
static uint8_t                 g_vehicle_door_state = PHONE_DOOR_STATE_UNKNOWN;
static bool                    g_auth_condition_met = true;  /* PEPS/车辆授权条件 (串口可控) */

/* 延迟处理: 从 BLE 事件回调中缓存帧, 在主循环 phone_sm_process_action 中处理
 * 避免 BLE 事件处理阻塞 CAN 等实时任务 */
static uint8_t  g_pending_rx_data[PHONE_MAX_ENCRYPTED_FRAME_LEN];
static uint16_t g_pending_rx_len = 0U;

/* 第二阶段帧处理: 解密+解析完成后, dispatch 延迟到下一轮 (避免 BLE send 阻塞主循环) */
static struct {
    bool     active;
    uint8_t  cmd;
    uint16_t seq;
    bool     is_encrypted;
    bool     is_bind_cmd;
    uint8_t  payload[PHONE_MAX_PAYLOAD_LEN];
    uint16_t payload_len;
} g_frame_dispatch;

/* 延迟 ECDSA 验签: 将耗时的 psa_verify_hash 拆成两步
 * Step 1: 命令处理器在构造好 SHA-256 digest 后保存参数, 设置 active=true, 提前返回
 * Step 2: phone_sm_process_action 在下一轮主循环中执行实际验签 + 后处理
 * 这样 CAN 可以在两步之间运行, 避免单次主循环迭代过长 */
static struct {
    bool     active;
    uint8_t  cmd;
    uint16_t seq;
    uint8_t  pub_key[65];
    uint8_t  digest[32];
    uint8_t  sig[64];
    uint64_t app_counter;
    uint8_t  control_cmd;
    uint8_t  shared_secret[32];
    uint8_t  token_body[256];   /* QR_VERIFY: 验签后校验字段 */
    uint16_t token_body_len;
} g_ecdsa_defer;

/* 延迟 AUTH_CHALLENGE_REQ: ECDH 密钥生成 (psa_generate_key) 阻塞 ~400ms,
 * 拆分到独立主循环步骤, 避免阻塞 CAN */
static struct {
    bool     active;
    uint16_t seq;
    uint8_t  stored_key_id[16];
} g_auth_defer;

/* 延迟 CTRL_CHALLENGE_REQ: send_encrypted_response 耗时 ~500ms,
 * 拆分到独立主循环步骤, CAN 优先运行 */
static struct {
    bool     active;
    uint16_t seq;
} g_ctrl_challenge_defer;

/* 延迟 AUTH_CHALLENGE_RSP step1: ECDH 共享密钥计算
 * (phone_crypto_ecdh_compute_shared, PSA→SE, 同步 ~400ms, 必须是第一步延迟) */
static struct {
    bool     active;
    uint16_t seq;
    uint64_t app_counter;
    uint8_t  pub_key[65];    /* stored APP public key for verify */
    uint8_t  sig[64];        /* appSignature from TLV */
} g_ecdh_rsp_defer;

/* ========================================================================== */
/* 前向声明: 命令处理函数                                                      */
/* ========================================================================== */

static void handle_get_device_info(uint16_t seq);
static void handle_bind_hello(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_qr_verify(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_bind_window_query(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_register_app_key(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_auth_challenge_req(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_auth_challenge_rsp(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_get_status(uint16_t seq);
static void handle_rebind_request(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_rebind_register_key(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_ctrl_challenge_req(uint16_t seq);
static void tx_dump_and_send(const uint8_t *buf, uint16_t out_len, uint8_t cmd, uint16_t seq);
static void tx_event_dump_and_send(const uint8_t *buf, uint16_t out_len);
static void handle_ctrl_command(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_passive_pair_prepare(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_passive_pair_ready(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_passive_pair_cancel(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_passive_enable(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_passive_disable(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_passive_sensitivity_set(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);
static void handle_passive_quota_refresh(uint16_t seq, const phone_tlv_t *tlvs, uint8_t tlv_count);

/* ========================================================================== */
/* V1.2: 配对上下文清理 (CANCEL/超时/bonded/bonding_failed 共用)              */
/* ========================================================================== */

/** 销毁配对窗口, 恢复默认 SM 配置 (SC+BR, NoIO, Non-Bondable) */
static void pairing_context_destroy(void)
{
  /* 恢复默认 SM 配置 + 关闭 Bondable (规范 23.2: 窗口结束立即恢复 Non-Bondable) */
  (void)sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_SC_ONLY
                           | SL_BT_SM_CONFIGURATION_BONDING_REQUIRED,
                           sl_bt_sm_io_capability_noinputnooutput);
  (void)sl_bt_sm_set_bondable_mode(0);
  g_sess.pairing_window_active = false;
  g_sess.pairing_awaiting_system = false;
  hid_service_set_runtime_enabled(g_sess.passive_enabled);
  phone_crypto_memzero(g_sess.pairing_window_id, 8U);
  g_sess.pairing_passkey = 0U;
}

/* ========================================================================== */
/* 内部: CCM Nonce 构造                                                       */
/* ========================================================================== */

static void build_ccm_nonce(uint8_t nonce[13], uint8_t direction,
                             const uint8_t session_short[4], uint64_t counter)
{
  nonce[0] = direction;
  memcpy(&nonce[1], session_short, 4U);
  /* SecurityCounter U64 BE → bytes [5..12] */
  uint8_t i;
  for (i = 0U; i < 8U; i++) {
    nonce[5U + i] = (uint8_t)(counter >> (56U - 8U * i));
  }
}

/* ========================================================================== */
/* 内部: 构造 AAD = Header(11) + SecurityCounter(8)                          */
/* ========================================================================== */

static uint16_t build_aad(uint8_t aad[19], uint8_t cmd, uint8_t msg_type, uint8_t flags,
                           uint16_t seq, uint16_t payload_len, uint64_t counter)
{
  aad[0] = PHONE_FRAME_MAGIC;
  aad[1] = PHONE_FRAME_VERSION;
  aad[2] = msg_type;
  aad[3] = cmd;
  phone_frame_write_u16_be(&aad[4], seq);
  aad[6] = flags;
  aad[7] = PHONE_FRAG_INDEX;
  aad[8] = PHONE_FRAG_TOTAL;
  phone_frame_write_u16_be(&aad[9], payload_len);
  /* SecurityCounter */
  uint8_t i;
  for (i = 0U; i < 8U; i++) {
    aad[11U + i] = (uint8_t)(counter >> (56U - 8U * i));
  }
  return 19U;
}

/* ========================================================================== */
/* 内部: 获取 sessionShort (sessionKey 需调用者区分)                          */
/* ========================================================================== */

static void get_bind_session_short(uint8_t short_id[4])
{
  memcpy(short_id, g_sess.bind_session_id, 4U);
}

static void get_session_short(uint8_t short_id[4])
{
  memcpy(short_id, g_sess.auth_session_id, 4U);
}

/* ========================================================================== */
/* 内部: 发送响应 / 事件                                                       */
/* ========================================================================== */

/** 发送明文响应帧 */
static sl_status_t send_plain_response(uint8_t cmd, uint16_t seq,
                                       const uint8_t *payload, uint16_t payload_len)
{
  static uint8_t buf[PHONE_MAX_PLAIN_FRAME_LEN];  /* static 省栈 */
  uint16_t out_len;
  sl_status_t sc;

  if (g_send_fn == NULL) return SL_STATUS_FAIL;

  sc = phone_frame_encode_plain(cmd, PHONE_MSGTYPE_RESPONSE, PHONE_FLAGS_PLAIN_RSP,
                                seq, payload, payload_len,
                                buf, &out_len, sizeof(buf));
  if (sc != SL_STATUS_OK) return sc;

  /* 解析 Payload 中的 TLV 并打印 */
  {
    static phone_tlv_t tx_tlvs[12];  /* static 省栈 */
    uint8_t tx_cnt = 0U;
    if (payload_len > 0U) {
      (void)phone_tlv_parse(payload, payload_len, tx_tlvs, &tx_cnt, 12U);
    }
    dump_tx_frame(buf, out_len, cmd, seq, PHONE_MSGTYPE_RESPONSE,
                  tx_tlvs, tx_cnt, false);
  }

  (void)g_send_fn(buf, out_len);
  phone_session_cache_idempotent(&g_sess, cmd, seq, buf, out_len);
  return SL_STATUS_OK;
}

/** 发送加密响应帧 (使用 sessionKey 或 bindSessionKey, 含 AAD) */
static sl_status_t send_encrypted_response(uint8_t cmd, uint16_t seq,
                                           const uint8_t *plain_payload, uint16_t plain_payload_len,
                                           bool is_bind)
{
  uint8_t buf[PHONE_MAX_ENCRYPTED_FRAME_LEN];
  uint8_t nonce[13];
  uint8_t aad[19];
  uint8_t ciphertext[PHONE_MAX_PAYLOAD_LEN];
  uint8_t tag[8];
  uint64_t tx_counter;
  uint16_t out_len;
  uint8_t session_short[4];
  const uint8_t *enc_key;
  sl_status_t sc;

  /* 选择密钥: 绑定阶段用 bind_session_key, 业务阶段用 session_key */
  if (is_bind) {
    enc_key = g_sess.bind_session_key;
    get_bind_session_short(session_short);
  } else {
    enc_key = g_sess.session_key;
    get_session_short(session_short);
  }

  USER_LOG_INFO("[SM] TX %s seq=%u encrypted_response start plen=%u key=%s" USER_LOG_NL,
                cmd_name(cmd), (unsigned)seq, (unsigned)plain_payload_len,
                is_bind ? "bindKey" : "sessionKey");

  /* step0: 检查发送函数 */
  if (g_send_fn == NULL) {
    USER_LOG_INFO("[SM] TX %s seq=%u FAIL: g_send_fn is NULL" USER_LOG_NL,
                  cmd_name(cmd), (unsigned)seq);
    return SL_STATUS_FAIL;
  }

  /* 先解析 TLV (加密前), 用于日志打印 */
  phone_tlv_t tx_tlvs[12];
  uint8_t tx_cnt = 0U;
  if (plain_payload_len > 0U) {
    (void)phone_tlv_parse(plain_payload, plain_payload_len, tx_tlvs, &tx_cnt, 12U);
  }

  /* counter + nonce */
  tx_counter = phone_session_next_tx_counter(&g_sess);
  build_ccm_nonce(nonce, PHONE_AES_CCM_DIR_BG24_TO_APP, session_short, tx_counter);

  /* 构造 AAD = FrameHeader(11) + SecurityCounter(8) */
  build_aad(aad, cmd, PHONE_MSGTYPE_RESPONSE, PHONE_FLAGS_ENCRYPTED_RSP,
            seq, plain_payload_len, tx_counter);

  /* AES-CCM 加密 */
  sc = phone_crypto_aes_ccm_encrypt(enc_key, nonce,
                                     aad, 19U,
                                     plain_payload, plain_payload_len,
                                     ciphertext, tag);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] TX %s seq=%u FAIL: CCM encrypt sc=0x%04lX" USER_LOG_NL,
                  cmd_name(cmd), (unsigned)seq, (unsigned long)sc);
    return sc;
  }

#ifdef PHONE_DUMP_FRAMES
  /* ===== TX 加密详细参数 (供 APP 端对照调试) ===== */
  USER_LOG_INFO("[SM] === %s_RSP Encryption Debug ===" USER_LOG_NL, cmd_name(cmd));
  USER_LOG_INFO("[SM]   is_bind=%d" USER_LOG_NL, (int)is_bind);
  USER_LOG_INFO("[SM]   enc_key[16]:          %s" USER_LOG_NL,
                hex_str(enc_key, 16U, 16U));
  USER_LOG_INFO("[SM]   sessionShort[4] %s:  %s" USER_LOG_NL,
                is_bind ? "(bindSessionId[0:4])" : "(authSessionId[0:4])",
                hex_str(session_short, 4U, 4U));
  if (is_bind) {
    USER_LOG_INFO("[SM]   bindSessionId[16]:    %s" USER_LOG_NL,
                  hex_str(g_sess.bind_session_id, 16U, 16U));
  }
  USER_LOG_INFO("[SM]   tx_counter:            %s (dec=%s)" USER_LOG_NL,
                u64_hex_str(tx_counter), u64_dec_str(tx_counter));
  USER_LOG_INFO("[SM]   Nonce[13]:             %s" USER_LOG_NL,
                hex_str_full(nonce, 13U));
  USER_LOG_INFO("[SM]   AAD[19]:               %s" USER_LOG_NL,
                hex_str_full(aad, 19U));
  USER_LOG_INFO("[SM]   Plaintext[%u]:         %s" USER_LOG_NL,
                (unsigned)plain_payload_len,
                hex_str_full(plain_payload, plain_payload_len));
  USER_LOG_INFO("[SM]   Ciphertext[%u]:        %s" USER_LOG_NL,
                (unsigned)plain_payload_len,
                hex_str_full(ciphertext, plain_payload_len));
  USER_LOG_INFO("[SM]   Tag[8]:                %s" USER_LOG_NL,
                hex_str_full(tag, 8U));
  USER_LOG_INFO("[SM] ====================================" USER_LOG_NL);
#endif

  /* 组装加密帧 */
  sc = phone_frame_encode_encrypted(cmd, PHONE_MSGTYPE_RESPONSE, PHONE_FLAGS_ENCRYPTED_RSP,
                                    seq, tx_counter, ciphertext, plain_payload_len, tag,
                                    buf, &out_len, sizeof(buf));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] TX %s seq=%u FAIL: frame_encode sc=0x%04lX" USER_LOG_NL,
                  cmd_name(cmd), (unsigned)seq, (unsigned long)sc);
    return sc;
  }

  /* dump + send */
  dump_tx_frame(buf, out_len, cmd, seq, PHONE_MSGTYPE_RESPONSE,
                tx_tlvs, tx_cnt, true);

  (void)g_send_fn(buf, out_len);
  g_cmd_ok = true;  /* 成功响应已发送 */
  phone_session_cache_idempotent(&g_sess, cmd, seq, buf, out_len);
  USER_LOG_INFO("[SM] TX %s seq=%u done is_bind=%d" USER_LOG_NL,
                cmd_name(cmd), (unsigned)seq, (int)is_bind);
  return SL_STATUS_OK;
}

/** 发送加密 Event 帧 (使用 sessionKey, Seq=0, 含 AAD) */
static sl_status_t send_encrypted_event(uint8_t cmd,
                                        const uint8_t *plain_payload, uint16_t plain_payload_len)
{
  uint8_t buf[PHONE_MAX_ENCRYPTED_FRAME_LEN];
  uint8_t nonce[13];
  uint8_t aad[19];
  uint8_t ciphertext[PHONE_MAX_PAYLOAD_LEN];
  uint8_t tag[8];
  uint64_t tx_counter;
  uint16_t out_len;
  uint8_t session_short[4];
  sl_status_t sc;

  if (g_send_fn == NULL) return SL_STATUS_FAIL;

  /* 先解析 TLV (加密前), 用于日志打印 */
  phone_tlv_t tx_tlvs[12];  /* 最多~8 TLV, 12 够用 */
  uint8_t tx_cnt = 0U;
  if (plain_payload_len > 0U) {
    (void)phone_tlv_parse(plain_payload, plain_payload_len, tx_tlvs, &tx_cnt, 12U);
  }

  tx_counter = phone_session_next_tx_counter(&g_sess);
  get_session_short(session_short);
  build_ccm_nonce(nonce, PHONE_AES_CCM_DIR_BG24_TO_APP, session_short, tx_counter);

  /* AAD = Header + SecurityCounter */
  build_aad(aad, cmd, PHONE_MSGTYPE_EVENT, PHONE_FLAGS_EVENT,
            0U, plain_payload_len, tx_counter);

  /* AES-CCM 加密 (含 AAD) */
  sc = phone_crypto_aes_ccm_encrypt(g_sess.session_key, nonce,
                                     aad, 19U,
                                     plain_payload, plain_payload_len,
                                     ciphertext, tag);
  if (sc != SL_STATUS_OK) return sc;

  sc = phone_frame_encode_encrypted(cmd, PHONE_MSGTYPE_EVENT, PHONE_FLAGS_EVENT,
                                    0U, tx_counter, ciphertext, plain_payload_len, tag,
                                    buf, &out_len, sizeof(buf));
  if (sc != SL_STATUS_OK) return sc;

  dump_tx_frame(buf, out_len, cmd, 0U, PHONE_MSGTYPE_EVENT,
                tx_tlvs, tx_cnt, true);

  (void)g_send_fn(buf, out_len);
  return SL_STATUS_OK;
}

/** 发送简单错误响应 (含 result + errorCode TLV, 可选 bindState + remainSec) */
static void send_error_response(uint8_t cmd, uint16_t seq,
                                uint8_t result, uint16_t error_code,
                                bool encrypted, bool with_bind_state,
                                bool with_remain_sec)
{
  uint8_t payload[64];
  uint16_t plen = 0U;
  phone_tlv_t tlvs[5];
  uint8_t tcnt = 0U;
  uint8_t u8;

  USER_LOG_INFO("[SM] %s seq=%u ERR result=%u code=0x%04X" USER_LOG_NL,
                cmd_name(cmd), (unsigned)seq, (unsigned)result, (unsigned)error_code);

  /* result */
  tlvs[tcnt].type  = PHONE_TLV_RESULT;
  tlvs[tcnt].len   = 1U;
  u8 = result;
  tlvs[tcnt].value = &u8;
  tcnt++;

  /* errorCode (U16 BE) */
  {
    static uint8_t ec_buf[2];
    phone_frame_write_u16_be(ec_buf, error_code);
    tlvs[tcnt].type  = PHONE_TLV_ERROR_CODE;
    tlvs[tcnt].len   = 2U;
    tlvs[tcnt].value = ec_buf;
    tcnt++;
  }

  /* bindState (可选, 多数命令失败响应需要) */
  if (with_bind_state) {
    u8 = g_sess.device_state;
    tlvs[tcnt].type  = PHONE_TLV_BIND_STATE;
    tlvs[tcnt].len   = 1U;
    tlvs[tcnt].value = &u8;
    tcnt++;
  }

  /* remainSec (可选, QR_VERIFY 等绑定阶段命令需要) */
  if (with_remain_sec) {
    static uint8_t rs_buf[2];
    uint64_t remain = 0ULL;
    if (g_sess.bind_session_active && g_sess.bind_deadline_ms > phone_session_now_ms()) {
      remain = (g_sess.bind_deadline_ms - phone_session_now_ms()) / 1000ULL;
    } else if (g_sess.is_silent && g_sess.silent_until_ms > phone_session_now_ms()) {
      remain = (g_sess.silent_until_ms - phone_session_now_ms()) / 1000ULL;
    }
    phone_frame_write_u16_be(rs_buf, (uint16_t)(remain > 65535ULL ? 65535U : (uint16_t)remain));
    tlvs[tcnt].type  = PHONE_TLV_REMAIN_SEC;
    tlvs[tcnt].len   = 2U;
    tlvs[tcnt].value = rs_buf;
    tcnt++;
  }

  if (phone_tlv_encode(tlvs, tcnt, payload, &plen, sizeof(payload)) != SL_STATUS_OK) return;

  /* 绑定阶段命令 (0x10/0x11/0x12/0x30/0x31): 绑定会话活跃时错误响应也用
   * bind_session_key 加密 (协议 §9.2 绑定阶段加密命令, 与成功响应/请求一致) */
  bool is_bind_cmd = (cmd == PHONE_CMD_QR_VERIFY
                   || cmd == PHONE_CMD_BIND_WINDOW_QUERY
                   || cmd == PHONE_CMD_REGISTER_APP_KEY
                   || cmd == PHONE_CMD_REBIND_REQUEST
                   || cmd == PHONE_CMD_REBIND_REGISTER_KEY);
  if (is_bind_cmd && phone_session_is_bind_active(&g_sess)) {
    (void)send_encrypted_response(cmd, seq, payload, plen, true);
  } else if (encrypted && phone_session_is_authenticated(&g_sess)) {
    (void)send_encrypted_response(cmd, seq, payload, plen, false);
  } else {
    (void)send_plain_response(cmd, seq, payload, plen);
  }
}

/* ========================================================================== */
/* 内部: 发送带 hex dump 的响应                                                 */
/* ========================================================================== */

static void tx_dump_and_send(const uint8_t *buf, uint16_t out_len, uint8_t cmd, uint16_t seq)
{
  if (g_send_fn == NULL) return;
  (void)g_send_fn(buf, out_len);
}

static void tx_event_dump_and_send(const uint8_t *buf, uint16_t out_len)
{
  if (g_send_fn == NULL) return;
  (void)g_send_fn(buf, out_len);
}

/* ========================================================================== */
/* 内部: TLV 必选字段校验                                                     */
/* ========================================================================== */

static bool check_tlv_lengths(const phone_tlv_t *tlvs, uint8_t count)
{
  uint8_t i;
  for (i = 0U; i < count; i++) {
    if (!phone_tlv_validate_length(tlvs[i].type, tlvs[i].len)) {
      USER_LOG_INFO("[SM] TLV_LEN_ERR type=0x%02X len=%u — 长度不符合该类型要求" USER_LOG_NL,
                    (unsigned)tlvs[i].type, (unsigned)tlvs[i].len);
      return false;
    }
  }
  return true;
}

/* ========================================================================== */
/* 内部: 接收流水线 (统一入口)                                                */
/* ========================================================================== */

static void process_received_frame(const uint8_t *data, uint16_t len)
{
  phone_frame_t frame;
  phone_tlv_t   tlvs[24];  /* 实际≤7 TLV/帧, 24安全并省栈64B */
  uint8_t       tlv_count;
  sl_status_t   sc;
  uint16_t      seq;
  uint8_t       cmd;
  uint32_t      t_entry, t_stage;

  t_entry = CanManage_StubGetTimeMs();
  g_cmd_ok = false;  /* 每次命令处理前复位 */
  g_cmd_deferred = false;

  /* ---- 1. 帧解码 + CRC 校验 ---- */
  sc = phone_frame_decode(data, len, &frame);
  t_stage = CanManage_StubGetTimeMs();
  if (sc != SL_STATUS_OK) {
    /* 帧头非法(SL_STATUS_INVALID_PARAMETER=0x21) 或 CRC失败(SL_STATUS_INVALID_SIGNATURE=0x2C) */
    const char *err_name = (sc == SL_STATUS_INVALID_SIGNATURE) ? "CRC_ERR" : "HDR_ERR";
    USER_LOG_INFO("[SM] RX_RAW[%u] %s sc=0x%04lX: %s" USER_LOG_NL,
                  (unsigned)len, err_name, (unsigned long)sc, hex_str(data, len, 64U));
    return;
  }

  cmd = frame.cmd;
  seq = frame.seq;

  /* ---- 2. 明文帧处理 ---- */
  if (!frame.is_encrypted) {
    /* TLV 解析 */
    if (frame.payload_len > 0U) {
      sc = phone_tlv_parse(frame.payload, frame.payload_len,
                           tlvs, &tlv_count, 24U);
      if (sc != SL_STATUS_OK) {
        USER_LOG_INFO("[SM] RX TLV_PARSE_ERR sc=0x%04lX Payload[%u]: %s" USER_LOG_NL,
                      (unsigned long)sc, (unsigned)frame.payload_len,
                      hex_str(frame.payload, frame.payload_len, 128U));
        send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_PARAM_INVALID, false, true, false);
        return;
      }
      if (!check_tlv_lengths(tlvs, tlv_count)) {
        USER_LOG_INFO("[SM] RX TLV_LEN_ERR count=%u Payload[%u]: %s" USER_LOG_NL,
                      (unsigned)tlv_count, (unsigned)frame.payload_len,
                      hex_str(frame.payload, frame.payload_len, 128U));
        send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_PARAM_INVALID, false, true, false);
        return;
      }
    } else {
      tlv_count = 0U;
    }

    /* 集中打印 RX 数据: 原始hex + 帧头 + TLV详情 */
    dump_rx_frame(data, len, &frame, tlvs, tlv_count);

    /* 按 cmd 分发 (明文命令) */
    switch (cmd) {
      case PHONE_CMD_GET_DEVICE_INFO:     /* handled below */ break;
      case PHONE_CMD_BIND_HELLO:           /* handled below */ break;
      case PHONE_CMD_AUTH_CHALLENGE_REQ:   /* handled below */ break;
      case PHONE_CMD_AUTH_CHALLENGE_RSP:   /* handled below */ break;
      default:
        /* 未知明文命令, 或应加密的命令被明文发送 */
        USER_LOG_INFO("[SM] RX PLAINTEXT_UNEXPECTED_CMD 0x%02X(%s) — maybe should be encrypted?" USER_LOG_NL,
                      (unsigned)cmd, cmd_name(cmd));
        send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_PARAM_INVALID, false, true, false);
        return;
    }

    /* ---- Dispatch 明文命令 ---- */
    switch (cmd) {
      case PHONE_CMD_GET_DEVICE_INFO:
        handle_get_device_info(seq);
        break;
      case PHONE_CMD_BIND_HELLO:
        handle_bind_hello(seq, tlvs, tlv_count);
        break;
      case PHONE_CMD_AUTH_CHALLENGE_REQ:
        handle_auth_challenge_req(seq, tlvs, tlv_count);
        break;
      case PHONE_CMD_AUTH_CHALLENGE_RSP:
        handle_auth_challenge_rsp(seq, tlvs, tlv_count);
        break;
      default:
        break;
    }
  }

  /* ---- 3. 加密帧处理 ---- */
  else {
    /* 加密帧必须区分绑定会话密钥 vs 业务会话密钥 */
    bool is_bind_cmd = (cmd == PHONE_CMD_QR_VERIFY
                     || cmd == PHONE_CMD_BIND_WINDOW_QUERY
                     || cmd == PHONE_CMD_REGISTER_APP_KEY
                     || cmd == PHONE_CMD_REBIND_REQUEST
                     || cmd == PHONE_CMD_REBIND_REGISTER_KEY);

    /* MTU 最低限制检查 (协议 §6/§22.1):
     *   绑定/换绑命令最低 MTU=219, 低于此值禁止绑定/换绑
     *   控制/状态命令最低 MTU=194, 低于此值禁止控制和状态 */
    if (is_bind_cmd && g_sess.att_mtu < PHONE_MTU_MIN_BIND) {
      USER_LOG_INFO("[SM] RX MTU_TOO_LOW for %s mtu=%u < min_bind=%u" USER_LOG_NL,
                    cmd_name(cmd), (unsigned)g_sess.att_mtu, (unsigned)PHONE_MTU_MIN_BIND);
      send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_UNSUPPORTED_VERSION, false, true, false);
      return;
    }
    if (!is_bind_cmd && g_sess.att_mtu < PHONE_MTU_MIN_CONTROL) {
      USER_LOG_INFO("[SM] RX MTU_TOO_LOW for %s mtu=%u < min_control=%u" USER_LOG_NL,
                    cmd_name(cmd), (unsigned)g_sess.att_mtu, (unsigned)PHONE_MTU_MIN_CONTROL);
      send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_UNSUPPORTED_VERSION, false, true, false);
      return;
    }

    const uint8_t *decrypt_key = NULL;
    uint8_t session_short[4] = {0};

    if (is_bind_cmd) {
      if (!phone_session_is_bind_active(&g_sess)) {
        USER_LOG_INFO("[SM] RX BIND_SESSION_MISSING for %s seq=%u" USER_LOG_NL,
                      cmd_name(cmd), (unsigned)seq);
        send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_STATE_NOT_ALLOWED, false, true, false);
        return;
      }
      decrypt_key = g_sess.bind_session_key;
      get_bind_session_short(session_short);
#ifdef PHONE_DUMP_FRAMES
      USER_LOG_INFO("[SM] %s seq=%u decrypt_key=bind_session_key: %s" USER_LOG_NL,
                    cmd_name(cmd), (unsigned)seq,
                    hex_str(decrypt_key, 16U, 16U));
#endif
    } else {
      if (!phone_session_is_authenticated(&g_sess)) {
        USER_LOG_INFO("[SM] RX SESSION_KEY_MISSING for %s seq=%u (not auth'd)" USER_LOG_NL,
                      cmd_name(cmd), (unsigned)seq);
        return;
      }
      decrypt_key = g_sess.session_key;
      get_session_short(session_short);
#ifdef PHONE_DUMP_FRAMES
      USER_LOG_INFO("[SM] %s seq=%u decrypt_key=session_key: %s" USER_LOG_NL,
                    cmd_name(cmd), (unsigned)seq,
                    hex_str(decrypt_key, 16U, 16U));
#endif
    }

    /* ---- 3.1 幂等缓存查询 ---- */
    {
      uint8_t  cached[256];
      uint16_t cached_len;
      if (phone_session_check_idempotent(&g_sess, cmd, seq, cached, &cached_len)) {
        /* 命中缓存: 直接返回原响应 */
#ifdef PHONE_DUMP_FRAMES
        USER_LOG_INFO("[SM] TX %s(RSP) Seq=%u Len=%u IDEMPOTENT_CACHE: %s" USER_LOG_NL,
                      cmd_name(cmd), (unsigned)seq,
                      (unsigned)cached_len, hex_str(cached, cached_len, 64U));
#endif
        if (g_send_fn != NULL) {
          (void)g_send_fn(cached, cached_len);
        }
        return;
      }
    }

    /* ---- 3.2 SecurityCounter 检查 (幂等未命中时执行) ---- */
    {
      sl_status_t ctr_sc = phone_session_check_rx_counter(&g_sess, frame.security_counter);
      if (ctr_sc != SL_STATUS_OK) {
        /* Counter 非法: 安全错误, 建议断开 */
        USER_LOG_INFO("[SM] RX_SecCtr_ERR %s Seq=%u Got=%s Expected=%s(last+1)" USER_LOG_NL,
                      cmd_name(cmd), (unsigned)seq,
                      u64_hex_str(frame.security_counter),
                      u64_hex_str(g_sess.security_counter_rx + 1ULL));
        phone_session_record_security_failure(&g_sess);
        if (g_disc_fn != NULL) g_disc_fn();
        return;
      }
    }

    /* ---- 3.3 AES-CCM 解密 + Tag 校验 ---- */
    {
      uint8_t nonce[13];
      uint8_t aad[19];
      uint8_t plaintext[PHONE_MAX_PAYLOAD_LEN];
      uint32_t t_ccm_start, t_ccm_end;

      build_ccm_nonce(nonce, PHONE_AES_CCM_DIR_APP_TO_BG24, session_short,
                      frame.security_counter);

      /* AAD = FrameHeader + SecurityCounter */
      build_aad(aad, cmd, frame.msg_type, frame.flags,
                seq, frame.payload_len, frame.security_counter);

#ifdef PHONE_DUMP_FRAMES
      /* ===== 解密前打印全部参数 (无论成败) ===== */
      USER_LOG_INFO("[SM] %s seq=%u === CCM 解密开始 ===" USER_LOG_NL,
                    cmd_name(cmd), (unsigned)seq);
      USER_LOG_INFO("[SM]   decrypt_key[16]:        %s" USER_LOG_NL,
                    hex_str(decrypt_key, 16U, 16U));
      USER_LOG_INFO("[SM]   Nonce[13]:              %s" USER_LOG_NL,
                    hex_str(nonce, 13U, 13U));
      USER_LOG_INFO("[SM]   AAD[19]:                %s" USER_LOG_NL,
                    hex_str(aad, 19U, 19U));
      if (frame.payload_len > 0U) {
        USER_LOG_INFO("[SM]   CT[%u]:                 %s" USER_LOG_NL,
                      (unsigned)frame.payload_len,
                      hex_str(frame.payload, (frame.payload_len < 32U) ? frame.payload_len : 32U, 32U));
      } else {
        USER_LOG_INFO("[SM]   CT[0]:                  (empty)" USER_LOG_NL);
      }
      USER_LOG_INFO("[SM]   Tag[8]:                 %s" USER_LOG_NL,
                    hex_str(frame.auth_tag, 8U, 8U));
      USER_LOG_INFO("[SM]   SessionShort[4] (%s): %s" USER_LOG_NL,
                    is_bind_cmd ? "bindSessionId" : "authSessionId",
                    hex_str(session_short, 4U, 4U));
      USER_LOG_INFO("[SM]   Counter:                %s" USER_LOG_NL,
                    u64_hex_str(frame.security_counter));
      /* 派生原数据 (APP 对照用) */
      USER_LOG_INFO("[SM]   === SessionKey 派生原数据 ===" USER_LOG_NL);
      USER_LOG_INFO("[SM]   nonce_a[16]:            %s" USER_LOG_NL,
                    hex_str_full(g_sess.nonce_a, 16U));
      USER_LOG_INFO("[SM]   nonce_b[16]:            %s" USER_LOG_NL,
                    hex_str_full(g_sess.nonce_b, 16U));
      USER_LOG_INFO("[SM]   auth_session_id[16]:    %s" USER_LOG_NL,
                    hex_str_full(g_sess.auth_session_id, 16U));
      {
        char did[33] = {0};
        uint8_t kid[16];
        phone_storage_get_device_id(did, sizeof(did) - 1U);
        phone_storage_get_app_key_id(kid);
        USER_LOG_INFO("[SM]   device_id:              \"%s\" (len=%u)" USER_LOG_NL,
                      did, (unsigned)strlen(did));
        USER_LOG_INFO("[SM]   app_key_id[16]:         %s" USER_LOG_NL,
                      hex_str_full(kid, 16U));
      }
#endif

      t_ccm_start = CanManage_StubGetTimeMs();
      sc = phone_crypto_aes_ccm_decrypt(decrypt_key, nonce,
                                         aad, 19U,
                                         frame.payload, frame.payload_len,
                                         frame.auth_tag,
                                         plaintext);
      t_ccm_end = CanManage_StubGetTimeMs();
      if (sc == SL_STATUS_INVALID_SIGNATURE) {
        USER_LOG_INFO("[SM] %s seq=%u ❌ CCM_TAG_INVALID fail#%u" USER_LOG_NL,
                      cmd_name(cmd), (unsigned)seq,
                      (unsigned)(g_sess.security_fail_count + 1U));
        phone_session_record_security_failure(&g_sess);
        return;
      }
      if (sc != SL_STATUS_OK) {
        USER_LOG_INFO("[SM] %s seq=%u ❌ AES-CCM decrypt fail sc=0x%04lX" USER_LOG_NL,
                      cmd_name(cmd), (unsigned)seq, (unsigned long)sc);
        return;  /* 解密失败, 静默丢弃 */
      }

      USER_LOG_INFO("[SM] %s seq=%u ✅ CCM decrypt OK 耗时=%lums" USER_LOG_NL,
                    cmd_name(cmd), (unsigned)seq,
                    (unsigned long)(t_ccm_end - t_ccm_start));

      /* 更新已接受 Counter */
      phone_session_accept_rx_counter(&g_sess, frame.security_counter);

      /* ---- 3.4 TLV 解析 ---- */
      if (frame.payload_len > 0U) {
        sc = phone_tlv_parse(plaintext, frame.payload_len,
                             tlvs, &tlv_count, 24U);
        if (sc != SL_STATUS_OK) {
          USER_LOG_INFO("[SM] RX_TLV_PARSE_ERR %s sc=0x%04lX Decrypted[%u]: %s" USER_LOG_NL,
                        cmd_name(cmd), (unsigned long)sc, (unsigned)frame.payload_len,
                        hex_str_full(plaintext, frame.payload_len));
          send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_PARAM_INVALID,
                              !is_bind_cmd, true, false);
          return;
        }
        if (!check_tlv_lengths(tlvs, tlv_count)) {
          USER_LOG_INFO("[SM] RX_TLV_LEN_ERR %s count=%u Decrypted[%u]: %s" USER_LOG_NL,
                        cmd_name(cmd), (unsigned)tlv_count, (unsigned)frame.payload_len,
                        hex_str_full(plaintext, frame.payload_len));
          send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_PARAM_INVALID,
                              !is_bind_cmd, true, false);
          return;
        }
      } else {
        tlv_count = 0U;
      }

      /* 集中打印 RX 数据: 原始hex + 帧头 + TLV详情 */
      dump_rx_frame(data, len, &frame, tlvs, tlv_count);

      /* ---- Dispatch 加密命令 ---- */
      switch (cmd) {
        case PHONE_CMD_QR_VERIFY:
          handle_qr_verify(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_BIND_WINDOW_QUERY:
          handle_bind_window_query(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_REGISTER_APP_KEY:
          handle_register_app_key(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_REBIND_REQUEST:
          handle_rebind_request(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_REBIND_REGISTER_KEY:
          handle_rebind_register_key(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_GET_STATUS:
          handle_get_status(seq);
          break;
        case PHONE_CMD_CTRL_CHALLENGE_REQ:
          handle_ctrl_challenge_req(seq);
          break;
        case PHONE_CMD_CTRL_COMMAND:
          handle_ctrl_command(seq, tlvs, tlv_count);
          break;
        /* V1.2 PASSIVE 命令 (is_bind_cmd=false → sessionKey 路径) */
        case PHONE_CMD_PASSIVE_PAIR_PREPARE:
          handle_passive_pair_prepare(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_PASSIVE_PAIR_READY:
          handle_passive_pair_ready(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_PASSIVE_PAIR_CANCEL:
          handle_passive_pair_cancel(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_PASSIVE_ENABLE:
          handle_passive_enable(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_PASSIVE_DISABLE:
          handle_passive_disable(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_PASSIVE_SENSITIVITY_SET:
          handle_passive_sensitivity_set(seq, tlvs, tlv_count);
          break;
        case PHONE_CMD_PASSIVE_QUOTA_REFRESH:
          handle_passive_quota_refresh(seq, tlvs, tlv_count);
          break;
        default:
          send_error_response(cmd, seq, PHONE_RESULT_FAIL, PHONE_ERR_PARAM_INVALID,
                              !is_bind_cmd, true, false);
          break;
      }

      /* 帧处理总耗时 (含解密+验签+响应) */
      {
        uint32_t t_total = CanManage_StubGetTimeMs();
        USER_LOG_INFO("[SM] %s seq=%u %s 耗时=%lums (decode=%lums CCM=%lums)" USER_LOG_NL,
                      cmd_name(cmd), (unsigned)seq,
                      g_cmd_ok ? "✅ 成功" : (g_cmd_deferred ? "⏳ 延迟处理" : "❌ 失败"),
                      (unsigned long)(t_total - t_entry),
                      (unsigned long)(t_stage - t_entry),
                      (unsigned long)(t_ccm_end - t_ccm_start));
      }
    }
  }
}

/* ========================================================================== */
/* Cmd 0x01: GET_DEVICE_INFO (明文, 第 22.3 节)                               */
/* ========================================================================== */

static void handle_get_device_info(uint16_t seq)
{
  uint8_t payload[256];
  uint16_t plen;
  phone_tlv_t tlvs[8];
  uint8_t tcnt = 0U;
  uint8_t u8_result, u8_proto_ver, u8_bind_state;
  uint32_t u32;
  char did[PHONE_DEVICE_ID_MAX_LEN + 1];
  uint8_t fw[3];
  uint8_t caps_buf[4];

  /* result = OK */
  u8_result = PHONE_RESULT_OK;
  tlvs[tcnt].type  = PHONE_TLV_RESULT;
  tlvs[tcnt].len   = 1U;
  tlvs[tcnt].value = &u8_result;
  tcnt++;

  /* errorCode = 0 (使用本地变量) */
  uint8_t ec_buf3[2] = {0, 0};
  tlvs[tcnt].type  = PHONE_TLV_ERROR_CODE;
  tlvs[tcnt].len   = 2U;
  tlvs[tcnt].value = ec_buf3;
  tcnt++;

  /* deviceId */
  memset(did, 0, sizeof(did));
  if (phone_storage_get_device_id(did, sizeof(did) - 1U) == SL_STATUS_OK) {
    tlvs[tcnt].type  = PHONE_TLV_DEVICE_ID;
    tlvs[tcnt].len   = (uint16_t)strlen(did);
    tlvs[tcnt].value = (const uint8_t *)did;
    tcnt++;
  }

  /* protocolVersion */
  u8_proto_ver = PHONE_FRAME_VERSION;
  tlvs[tcnt].type  = PHONE_TLV_PROTOCOL_VERSION;
  tlvs[tcnt].len   = 1U;
  tlvs[tcnt].value = &u8_proto_ver;
  tcnt++;

  /* fwVersion (3 Byte) */
  fw[0] = PHONE_FW_VERSION_MAJOR;
  fw[1] = PHONE_FW_VERSION_MINOR;
  fw[2] = PHONE_FW_VERSION_PATCH;
  tlvs[tcnt].type  = PHONE_TLV_FW_VERSION;
  tlvs[tcnt].len   = 3U;
  tlvs[tcnt].value = fw;
  tcnt++;

  /* bindState */
  u8_bind_state = g_sess.device_state;
  tlvs[tcnt].type  = PHONE_TLV_BIND_STATE;
  tlvs[tcnt].len   = 1U;
  tlvs[tcnt].value = &u8_bind_state;
  tcnt++;

  /* capabilityFlags */
  u32 = PHONE_CAP_DEFAULT_V11;
  phone_frame_write_u32_be(caps_buf, u32);
  tlvs[tcnt].type  = PHONE_TLV_CAPABILITY_FLAGS;
  tlvs[tcnt].len   = 4U;
  tlvs[tcnt].value = caps_buf;
  tcnt++;

  /* 编码并发送 */
  if (phone_tlv_encode(tlvs, tcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
    /* 缓存响应用于同 Seq 查询 (虽然是明文, 也缓存) */
    uint8_t full_resp[PHONE_MAX_PLAIN_FRAME_LEN];
    uint16_t full_len;
    if (phone_frame_encode_plain(PHONE_CMD_GET_DEVICE_INFO, PHONE_MSGTYPE_RESPONSE,
                                  PHONE_FLAGS_PLAIN_RSP, seq,
                                  payload, plen,
                                  full_resp, &full_len, sizeof(full_resp)) == SL_STATUS_OK) {
      phone_session_cache_idempotent(&g_sess, PHONE_CMD_GET_DEVICE_INFO, seq,
                                     full_resp, full_len);
      dump_tx_frame(full_resp, full_len, PHONE_CMD_GET_DEVICE_INFO, seq,
                    PHONE_MSGTYPE_RESPONSE, tlvs, tcnt, false);
      tx_dump_and_send(full_resp, full_len, PHONE_CMD_GET_DEVICE_INFO, seq);
    }
  }
}

/* ========================================================================== */
/* Cmd 0x0F: BIND_HELLO (明文, 第 9.1 / 22.3 节)                             */
/* ========================================================================== */

static void handle_bind_hello(uint16_t seq,
                              const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 已在 process_received_frame 中集中打印 */

  /* ===== 必选TLV校验 ===== */
  const uint8_t req_tlvs[] = {PHONE_TLV_DEVICE_ID, PHONE_TLV_QID,
                              PHONE_TLV_CV, PHONE_TLV_APP_BIND_NONCE};
  const phone_tlv_t *tlv;
  static uint8_t payload[128];  /* static 省栈, BIND_HELLO 回应 TLV ≤56B */
  uint16_t plen;
  phone_tlv_t rsp_tlvs[8];
  uint8_t rcnt = 0U;
  char did[PHONE_DEVICE_ID_MAX_LEN + 1];
  char qid[PHONE_TLV_MAX_QID + 1];
  uint32_t cv;
  uint8_t app_nonce[16];
  uint8_t bg_nonce[16];
  uint8_t session_id[16];
  uint8_t session_key[16];
  uint8_t bind_secret[16];
  uint8_t u8_r, u8_b;  /* result / bindState 各独立 */
  sl_status_t sc;

  USER_LOG_INFO("[SM] BIND_HELLO seq=%u START devState=0x%02X" USER_LOG_NL,
                (unsigned)seq, (unsigned)g_sess.device_state);

  /* 允许状态 */
  uint8_t state = g_sess.device_state;
  if (state != PHONE_DEVICE_STATE_UNBOUND && state != PHONE_DEVICE_STATE_BOUND
      && state != PHONE_DEVICE_STATE_REBIND_WINDOW) {
    USER_LOG_INFO("[SM] BIND_HELLO STATE_NOT_ALLOWED devState=0x%02X" USER_LOG_NL, (unsigned)state);
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, false, true, false);
    return;
  }

  USER_LOG_INFO("[SM] BIND_HELLO seq=%u step1 state_check OK" USER_LOG_NL, (unsigned)seq);

  /* SILENT 状态拦截 (§22.5): 拒绝, 返回 DEVICE_SILENT + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] BIND_HELLO SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    {
      uint8_t sil_payload[32];
      uint16_t sil_plen;
      phone_tlv_t sil_tlvs[4];
      uint8_t sil_cnt = 0U;
      uint8_t sil_r, sil_b;
      uint8_t sil_ec[2];
      uint8_t sil_rs[2];
      sil_r = PHONE_RESULT_FAIL;
      sil_tlvs[sil_cnt].type = PHONE_TLV_RESULT; sil_tlvs[sil_cnt].len = 1U; sil_tlvs[sil_cnt].value = &sil_r; sil_cnt++;
      phone_frame_write_u16_be(sil_ec, PHONE_ERR_DEVICE_SILENT);
      sil_tlvs[sil_cnt].type = PHONE_TLV_ERROR_CODE; sil_tlvs[sil_cnt].len = 2U; sil_tlvs[sil_cnt].value = sil_ec; sil_cnt++;
      sil_b = g_sess.device_state;
      sil_tlvs[sil_cnt].type = PHONE_TLV_BIND_STATE; sil_tlvs[sil_cnt].len = 1U; sil_tlvs[sil_cnt].value = &sil_b; sil_cnt++;
      phone_frame_write_u16_be(sil_rs, remain);
      sil_tlvs[sil_cnt].type = PHONE_TLV_REMAIN_SEC; sil_tlvs[sil_cnt].len = 2U; sil_tlvs[sil_cnt].value = sil_rs; sil_cnt++;
      if (phone_tlv_encode(sil_tlvs, sil_cnt, sil_payload, &sil_plen, sizeof(sil_payload)) == SL_STATUS_OK) {
        (void)send_plain_response(PHONE_CMD_BIND_HELLO, seq, sil_payload, sil_plen);
      }
    }
    return;
  }

  /* 必选 TLV 校验 */
  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_HELLO TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }

  USER_LOG_INFO("[SM] BIND_HELLO seq=%u step2 tlv_validate OK" USER_LOG_NL, (unsigned)seq);

  /* 提取 deviceId (与 NVM 比较) */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_DEVICE_ID);
  memset(did, 0, sizeof(did));
  memcpy(did, tlv->value, tlv->len);
  {
    char stored_did[PHONE_DEVICE_ID_MAX_LEN + 1];
    memset(stored_did, 0, sizeof(stored_did));
    phone_storage_get_device_id(stored_did, sizeof(stored_did) - 1U);
    if (strcmp(did, stored_did) != 0) {
      /* deviceId 不匹配 */
      USER_LOG_INFO("[SM] BIND_HELLO deviceId MISMATCH rcv='%s' my='%s'" USER_LOG_NL,
                    did, stored_did);
      if (g_disc_fn != NULL) g_disc_fn();
      return;
    }
  }

  /* 提取 qid (与 NVM currentQid 比较) */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_QID);
  memset(qid, 0, sizeof(qid));
  memcpy(qid, tlv->value, tlv->len);
  {
    char stored_qid[PHONE_TLV_MAX_QID + 1];
    memset(stored_qid, 0, sizeof(stored_qid));
    phone_storage_get_qid(stored_qid, sizeof(stored_qid));
    if (strcmp(qid, stored_qid) != 0) {
      USER_LOG_INFO("[SM] BIND_HELLO QID_MISMATCH rcv='%s' nvm='%s'" USER_LOG_NL,
                    qid, stored_qid);
      send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PARAM_INVALID, false, true, false);
      return;
    }
  }

  /* 提取 cv (必须等于 currentCv) */
  if (!phone_tlv_read_u32(tlvs, tlv_count, PHONE_TLV_CV, &cv)) {
    USER_LOG_INFO("[SM] BIND_HELLO CV_TLV_INVALID" USER_LOG_NL);
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }
  if (cv != phone_storage_get_cv()) {
    USER_LOG_INFO("[SM] BIND_HELLO CV_MISMATCH rcv=%lu nvm=%lu" USER_LOG_NL,
                  (unsigned long)cv, (unsigned long)phone_storage_get_cv());
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }

  /* 提取 appBindNonce */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_BIND_NONCE);
  memcpy(app_nonce, tlv->value, 16U);

  /* 生成 bgBindNonce + bindSessionId */
  sc = phone_crypto_get_random(bg_nonce, 16U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_HELLO TRNG_FAIL(bg_nonce) sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }
  sc = phone_crypto_get_random(session_id, 16U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_HELLO TRNG_FAIL(session_id) sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }

  /* 派生 bindSessionKey */
  sc = phone_storage_get_bind_secret(bind_secret);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_HELLO BIND_SECRET_READ_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }
  sc = phone_crypto_derive_bind_session_key(bind_secret, app_nonce, bg_nonce,
                                             did, qid, cv, session_key);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_HELLO HKDF_DERIVE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    phone_crypto_memzero(bind_secret, 16U);
    send_error_response(PHONE_CMD_BIND_HELLO, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }

  /* ===== 拼装 info 缓冲区 (用于完整打印) ===== */
  {
    uint8_t info_buf[128];
    uint8_t salt_buf[32];
    uint8_t prk_diag[32];
    uint16_t info_off = 0U;
    uint16_t did_len = (uint16_t)strlen(did);
    uint16_t qid_len = (uint16_t)strlen(qid);

    memcpy(info_buf + info_off, "BLEKEY-BIND-V1", 14U); info_off += 14U;
    memcpy(info_buf + info_off, did, did_len);          info_off += did_len;
    memcpy(info_buf + info_off, qid, qid_len);          info_off += qid_len;
    phone_frame_write_u32_be(info_buf + info_off, cv);  info_off += 4U;

    memcpy(salt_buf, app_nonce, 16U);
    memcpy(salt_buf + 16U, bg_nonce, 16U);

    /* 手动复算 PRK (与 phone_crypto_hkdf_sha256 内部逻辑一致) */
    (void)phone_crypto_hmac_sha256(salt_buf, 32U, bind_secret, 16U, prk_diag);

    /* ===== 打印 DKDF 结果 ===== */
#ifdef PHONE_DUMP_FRAMES
    USER_LOG_INFO("[SM] bindSessionKey[16]: %s" USER_LOG_NL,
                  hex_str(session_key, 16U, 16U));
#endif
  }

  phone_crypto_memzero(bind_secret, 16U);

  /* 开始绑定会话 */
  phone_session_begin_bind(&g_sess, app_nonce, bg_nonce, session_id, session_key,
                           (uint16_t)(PHONE_TIMEOUT_BIND_WINDOW_MS / 1000U));

  /* 构造成功响应 */
  /* result */
  u8_r = PHONE_RESULT_OK;
  rsp_tlvs[rcnt].type  = PHONE_TLV_RESULT;
  rsp_tlvs[rcnt].len   = 1U;
  rsp_tlvs[rcnt].value = &u8_r;
  rcnt++;

  /* errorCode */
  /* errorCode (使用本地变量) */
  uint8_t ec_buf2[2] = {0, 0};
  rsp_tlvs[rcnt].type  = PHONE_TLV_ERROR_CODE;
  rsp_tlvs[rcnt].len   = 2U;
  rsp_tlvs[rcnt].value = ec_buf2;
  rcnt++;

  /* bindSessionId */
  rsp_tlvs[rcnt].type  = PHONE_TLV_BIND_SESSION_ID;
  rsp_tlvs[rcnt].len   = 16U;
  rsp_tlvs[rcnt].value = g_sess.bind_session_id;
  rcnt++;

  /* bgBindNonce */
  rsp_tlvs[rcnt].type  = PHONE_TLV_BG_BIND_NONCE;
  rsp_tlvs[rcnt].len   = 16U;
  rsp_tlvs[rcnt].value = g_sess.bg_bind_nonce;
  rcnt++;

  /* remainSec */
  /* remainSec (使用本地变量) */
  uint8_t rs_buf2[2];
  phone_frame_write_u16_be(rs_buf2, (uint16_t)(PHONE_TIMEOUT_BIND_WINDOW_MS / 1000U));
  rsp_tlvs[rcnt].type  = PHONE_TLV_REMAIN_SEC;
  rsp_tlvs[rcnt].len   = 2U;
  rsp_tlvs[rcnt].value = rs_buf2;
  rcnt++;

  /* bindState */
  u8_b = g_sess.device_state;
  rsp_tlvs[rcnt].type  = PHONE_TLV_BIND_STATE;
  rsp_tlvs[rcnt].len   = 1U;
  rsp_tlvs[rcnt].value = &u8_b;
  rcnt++;

  if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_HELLO OK sessionId=%02X%02X%02X%02X..." USER_LOG_NL,
                  (unsigned)g_sess.bind_session_id[0], (unsigned)g_sess.bind_session_id[1],
                  (unsigned)g_sess.bind_session_id[2], (unsigned)g_sess.bind_session_id[3]);
    (void)send_plain_response(PHONE_CMD_BIND_HELLO, seq, payload, plen);
  }
}

/* ========================================================================== */
/* Cmd 0x10: QR_VERIFY (绑定加密帧, 第 9.2 / 22.3 节)                        */
/* ========================================================================== */

static void handle_qr_verify(uint16_t seq,
                             const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */

  /* ===== 第二步：必选TLV校验 ===== */
  const uint8_t req_tlvs[] = {PHONE_TLV_TOKEN_BODY, PHONE_TLV_SIG};
  const phone_tlv_t *tlv;
  sl_status_t sc;
  uint8_t digest[32];
  uint8_t factory_pubkey[65];
  char did[PHONE_DEVICE_ID_MAX_LEN + 1];

  USER_LOG_INFO("[SM] QR_VERIFY seq=%u" USER_LOG_NL, (unsigned)seq);

  /* SILENT 状态拦截 (§22.5): 拒绝, 返回 DEVICE_SILENT + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] QR_VERIFY SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_QR_VERIFY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, false, true, true);
    return;
  }

  /* 必选校验 */
  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] QR_VERIFY TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_QR_VERIFY, seq, PHONE_RESULT_FAIL, PHONE_ERR_PARAM_INVALID, false, true, true);
    return;
  }
  
  USER_LOG_INFO("[SM] QR_VERIFY TLV validation passed" USER_LOG_NL);

  /* ===== 厂家签名验签: SHA256(tokenBody) → ECDSA P-256 verify ===== */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_TOKEN_BODY);

  /* ---- Step 1: 打印原始 tokenBody ---- */
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[SM] ===== QR_VERIFY ECDSA 验签过程 =====" USER_LOG_NL);
  USER_LOG_INFO("[SM] 算法: ECDSA P-256 + SHA-256 (PSA_ALG_ECDSA(PSA_ALG_SHA_256), prehashed)" USER_LOG_NL);
  USER_LOG_INFO("[SM] 流程: SHA256(tokenBody) → 32字节 digest → ECDSA_verify(pubkey, digest, sig)" USER_LOG_NL);
  USER_LOG_INFO("[SM] --- Step 1: 原始 tokenBody (len=%u) ---" USER_LOG_NL, (unsigned)tlv->len);
  USER_LOG_INFO("[SM] tokenBody[%u] = %s" USER_LOG_NL, (unsigned)tlv->len, hex_str_full(tlv->value, tlv->len));
#endif

  /* ---- Step 2: SHA256(tokenBody) → digest ---- */
  sc = phone_crypto_sha256(tlv->value, tlv->len, digest);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] QR_VERIFY SHA256_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_QR_VERIFY, seq, PHONE_RESULT_FAIL, PHONE_ERR_INTERNAL_ERROR, false, true, true);
    return;
  }
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[SM] --- Step 2: SHA256(tokenBody) → digest[32] ---" USER_LOG_NL);
  USER_LOG_INFO("[SM] digest[32] = %s" USER_LOG_NL, hex_str_full(digest, 32U));
#endif

  /* ---- Step 3: 获取厂家公钥 (NVM 读取失败则拒绝, 绝不降级) ---- */
  {
    sc = phone_storage_get_factory_pubkey(factory_pubkey);
    if (sc != SL_STATUS_OK) {
      USER_LOG_INFO("[SM] QR_VERIFY FACTORY_PUBKEY_READ_FAIL sc=0x%04lX — 拒绝" USER_LOG_NL,
                    (unsigned long)sc);
      send_error_response(PHONE_CMD_QR_VERIFY, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_INTERNAL_ERROR, false, true, true);
      return;
    }
#ifdef PHONE_DUMP_FRAMES
    USER_LOG_INFO("[SM] --- Step 3: 厂家公钥 (NVM EEPROM) ---" USER_LOG_NL);
#endif
  }
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[SM] pubkey[65] = %s" USER_LOG_NL, hex_str_full(factory_pubkey, 65U));
#endif

  /* ---- Step 4: 签名 ---- */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_SIG);
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[SM] --- Step 4: APP 传来的签名 sig[64] ---" USER_LOG_NL);
  USER_LOG_INFO("[SM] sig[64] = %s" USER_LOG_NL, hex_str_full(tlv->value, 64U));
#endif

  /* ---- Step 5: ECDSA verify (延迟到异步 SE) ---- */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_SIG);
  g_cmd_deferred = true;
  g_ecdsa_defer.active       = true;
  g_ecdsa_defer.cmd          = PHONE_CMD_QR_VERIFY;
  g_ecdsa_defer.seq          = seq;
  memcpy(g_ecdsa_defer.pub_key, factory_pubkey, 65U);
  memcpy(g_ecdsa_defer.digest, digest, 32U);
  memcpy(g_ecdsa_defer.sig, tlv->value, 64U);
  /* 保存 tokenBody 用于验签后的字段校验 */
  {
    const phone_tlv_t *tb = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_TOKEN_BODY);
    uint16_t copy_len = (tb->len < sizeof(g_ecdsa_defer.token_body)) ? tb->len : (uint16_t)(sizeof(g_ecdsa_defer.token_body) - 1U);
    memcpy(g_ecdsa_defer.token_body, tb->value, copy_len);
    g_ecdsa_defer.token_body_len = copy_len;
  }
  USER_LOG_INFO("[SM] QR_VERIFY seq=%u → ECDSA deferred" USER_LOG_NL, (unsigned)seq);
  return;
}

/* ========================================================================== */
/* Cmd 0x11: BIND_WINDOW_QUERY (绑定加密帧, 第 9.2 / 22.3 节)                 */
/* ========================================================================== */

static void handle_bind_window_query(uint16_t seq,
                                     const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */
  
  /* ===== 第二步：必选TLV校验 ===== */
  const uint8_t req_tlvs[] = {PHONE_TLV_BIND_SESSION_ID};
  sl_status_t sc;

  USER_LOG_INFO("[SM] BIND_WINDOW_QUERY seq=%u" USER_LOG_NL, (unsigned)seq);

  /* SILENT 状态拦截 (§22.5): 拒绝, 返回 DEVICE_SILENT + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] BIND_WINDOW_QUERY SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_BIND_WINDOW_QUERY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, false, true, true);
    return;
  }

  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_WINDOW_QUERY TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_BIND_WINDOW_QUERY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, true);
    return;
  }

  /* 检查会话有效 + QR verified */
  if (!phone_session_is_bind_active(&g_sess) || !g_sess.qr_verified) {
    USER_LOG_INFO("[SM] BIND_WINDOW_QUERY SESSION_INVALID bind_active=%d qr_verified=%d" USER_LOG_NL,
                  (int)phone_session_is_bind_active(&g_sess), (int)g_sess.qr_verified);
    send_error_response(PHONE_CMD_BIND_WINDOW_QUERY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, false, true, true);
    return;
  }

  /* PEPS/车辆条件 (串口/外部可控, 默认满足) */
  if (!g_auth_condition_met) {
    USER_LOG_INFO("[SM] BIND_WINDOW_QUERY AUTH_COND_NOT_MET (g_auth_condition_met=false)" USER_LOG_NL);
    send_error_response(PHONE_CMD_BIND_WINDOW_QUERY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_COND_NOT_MET, false, true, true);
    return;
  }

  /* V1.2: 读取候选 VIN (PEPS/点火条件通过后, 设置 authorization 前) */
  if (!vehicle_state_is_vin_available()) {
    USER_LOG_INFO("[SM] BIND_WINDOW_QUERY VIN_NOT_AVAILABLE" USER_LOG_NL);
    send_error_response(PHONE_CMD_BIND_WINDOW_QUERY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_VEHICLE_ASSOCIATION_FAILED, false, true, true);
    return;
  }
  sc = vehicle_state_get_vin(g_sess.candidate_vin);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] BIND_WINDOW_QUERY VIN_READ_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_BIND_WINDOW_QUERY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_VEHICLE_ASSOCIATION_FAILED, false, true, true);
    return;
  }
  g_sess.candidate_vin_valid = true;
  USER_LOG_INFO("[SM] BIND_WINDOW_QUERY candidate_vin=%.17s" USER_LOG_NL,
                (const char *)g_sess.candidate_vin);

  phone_session_set_authorized(&g_sess);

  /* 成功响应 */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[4];
    uint8_t rcnt = 0U;
    uint8_t u8_r, u8_b;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;
    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    /* remainSec (使用本地变量) */
    uint8_t rs_buf[2];
    uint64_t remain = 0ULL;
    if (g_sess.bind_deadline_ms > phone_session_now_ms()) {
      remain = (g_sess.bind_deadline_ms - phone_session_now_ms()) / 1000ULL;
    }
    phone_frame_write_u16_be(rs_buf, (uint16_t)remain);
    rsp_tlvs[rcnt].type = PHONE_TLV_REMAIN_SEC; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = rs_buf; rcnt++;

    u8_b = g_sess.device_state;
    rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_BIND_WINDOW_QUERY, seq, payload, plen, true);
    }
  }
}

/* ========================================================================== */
/* Cmd 0x12: REGISTER_APP_KEY (绑定加密帧, 第 11.1 / 22.3 节)                 */
/* ========================================================================== */

static void handle_register_app_key(uint16_t seq,
                                    const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */
  
  const uint8_t req_tlvs[] = {PHONE_TLV_BIND_SESSION_ID,
                              PHONE_TLV_APP_PUBLIC_KEY, PHONE_TLV_PUBLIC_KEY_ALG};
  const phone_tlv_t *tlv;
  sl_status_t sc;
  uint8_t pubkey[65];
  uint8_t key_id[16];
  uint32_t bind_ver;

  USER_LOG_INFO("[SM] REGISTER_APP_KEY seq=%u" USER_LOG_NL, (unsigned)seq);

  /* SILENT 状态拦截 (§22.5): 拒绝, 返回 DEVICE_SILENT + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] REGISTER_APP_KEY SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, false, true, true);
    return;
  }

  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] REGISTER_APP_KEY TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }

  /* 状态校验 */
  if (g_sess.device_state != PHONE_DEVICE_STATE_UNBOUND
      || !phone_session_is_bind_active(&g_sess)
      || !g_sess.qr_verified
      || !g_sess.authorization_met) {
    USER_LOG_INFO("[SM] REGISTER_APP_KEY STATE_CHECK_FAIL state=%u bind=%d qr=%d auth=%d" USER_LOG_NL,
                  (unsigned)g_sess.device_state, (int)phone_session_is_bind_active(&g_sess),
                  (int)g_sess.qr_verified, (int)g_sess.authorization_met);
    send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, false, true, false);
    return;
  }

  /* 校验 publicKeyAlg (必须为 0x01 = ECDSA_P256_SHA256) */
  {
    uint8_t alg;
    phone_tlv_read_u8(tlvs, tlv_count, PHONE_TLV_PUBLIC_KEY_ALG, &alg);
    if (alg != PHONE_PUBKEY_ALG_ECDSA_P256_SHA256) {
      USER_LOG_INFO("[SM] REGISTER_APP_KEY PUBKEY_ALG_INVALID alg=0x%02X" USER_LOG_NL, (unsigned)alg);
      send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PARAM_INVALID, false, true, false);
      return;
    }
  }

  /* 提取并保存 APP 公钥 */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_PUBLIC_KEY);
  memcpy(pubkey, tlv->value, 65U);

  /* 生成 appKeyId (16 Byte, TRNG) */
  sc = phone_crypto_get_random(key_id, 16U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] REGISTER_APP_KEY TRNG_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }

  bind_ver = 1U;  /* 首次绑定 bindVersion = 1 */

  /* V1.2: VIN 复验 — 提交前再次确认当前 VIU VIN 与候选 VIN 一致 */
  if (!g_sess.candidate_vin_valid) {
    USER_LOG_INFO("[SM] REGISTER_APP_KEY NO_CANDIDATE_VIN" USER_LOG_NL);
    send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_VEHICLE_ASSOCIATION_FAILED, false, true, false);
    return;
  }
  {
    uint8_t cur_vin[17];
    if (!vehicle_state_is_vin_available()
        || vehicle_state_get_vin(cur_vin) != SL_STATUS_OK
        || memcmp(cur_vin, g_sess.candidate_vin, 17U) != 0) {
      USER_LOG_INFO("[SM] REGISTER_APP_KEY VIN_MISMATCH — 候选VIN与当前VIU VIN不一致" USER_LOG_NL);
      g_sess.candidate_vin_valid = false;
      send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_VEHICLE_ASSOCIATION_FAILED, false, true, false);
      return;
    }
  }

  /* 原子写入 (V1.2: 含 VIN, atomic_register 内部调用 user_vin_learn 更新 RAM) */
  sc = phone_storage_atomic_register(pubkey, key_id, bind_ver, g_sess.candidate_vin);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] REGISTER_APP_KEY ATOMIC_WRITE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    g_sess.candidate_vin_valid = false;
    send_error_response(PHONE_CMD_REGISTER_APP_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }

  USER_LOG_INFO("[SM] REGISTER_APP_KEY OK keyId=%02X%02X%02X%02X... bindVer=%lu VIN=%.17s" USER_LOG_NL,
                (unsigned)key_id[0], (unsigned)key_id[1],
                (unsigned)key_id[2], (unsigned)key_id[3],
                (unsigned long)bind_ver,
                (const char *)g_sess.candidate_vin);

  /* V1.2: VIN 已原子写入 EEPROM 并学习完成, 清除候选 */
  g_sess.candidate_vin_valid = false;

  /* 更新设备状态，但先不结束绑定会话 — send_encrypted_response 还需要 bind_session_key */
  g_sess.device_state = PHONE_DEVICE_STATE_BOUND;

  /* 成功响应 */
  {
    uint8_t payload[128];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[6];
    uint8_t rcnt = 0U;
    uint8_t u8_r, u8_b;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_APP_KEY_ID; rsp_tlvs[rcnt].len = 16U; rsp_tlvs[rcnt].value = key_id; rcnt++;

    /* bindVersion (使用本地变量) */
    uint8_t bv_buf[4];
    phone_frame_write_u32_be(bv_buf, bind_ver);
    rsp_tlvs[rcnt].type = PHONE_TLV_BIND_VERSION; rsp_tlvs[rcnt].len = 4U; rsp_tlvs[rcnt].value = bv_buf; rcnt++;

    u8_b = PHONE_DEVICE_STATE_BOUND;
    rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_REGISTER_APP_KEY, seq, payload, plen, true);
    }
  }

  /* 加密响应发送完成后才清除绑定会话 (否则 encrypt 拿到的是已清零的 key) */
  phone_session_end_bind(&g_sess);
}

/* ========================================================================== */
/* Cmd 0x20: AUTH_CHALLENGE_REQ (明文, 第 11.2 / 22.3 节)                     */
/* ========================================================================== */

static void handle_auth_challenge_req(uint16_t seq,
                                      const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */
  
  /* ===== 第二步：必选TLV校验 ===== */
  /* 协议 §22.3 冻结版: AUTH_CHALLENGE_REQ Request = appKeyId + nonceA + appEcdhPublicKey */
  const uint8_t req_tlvs[] = {PHONE_TLV_APP_KEY_ID, PHONE_TLV_NONCE,
                              PHONE_TLV_APP_ECDH_PUBLIC_KEY};
  const phone_tlv_t *tlv;
  sl_status_t sc;
  uint8_t stored_key_id[16];

  USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ seq=%u (V1.1 §22.3: appKeyId + nonceA + appEcdhPublicKey)" USER_LOG_NL,
                (unsigned)seq);

  /* SILENT 状态拦截 (§22.5): 拒绝, 返回 DEVICE_SILENT + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, false, true, true);
    return;
  }

  /* 状态校验: BOUND 且非 LOCKED (§22.3) */
  if (g_sess.device_state == PHONE_DEVICE_STATE_SECURITY_LOCKED) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ DEVICE_LOCKED" USER_LOG_NL);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_LOCKED, false, true, false);
    return;
  }
  if (g_sess.device_state != PHONE_DEVICE_STATE_BOUND) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ STATE_NOT_BOUND state=0x%02X" USER_LOG_NL,
                  (unsigned)g_sess.device_state);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, false, true, false);
    return;
  }

  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }

  /* 校验 appKeyId */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_KEY_ID);
  sc = phone_storage_get_app_key_id(stored_key_id);
  if (sc != SL_STATUS_OK || memcmp(tlv->value, stored_key_id, 16U) != 0) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ KEY_ID_MISMATCH rcv=%s nvm=%s" USER_LOG_NL,
                  hex_str(tlv->value, 16U, 16U), hex_str(stored_key_id, 16U, 16U));
    phone_session_record_security_failure(&g_sess);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_FAILED, false, true, false);
    return;
  }

  /* 提取 nonceA + appEcdhPublicKey (协议 §22.3: 在 AUTH_CHALLENGE_REQ 中提前传入) */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_NONCE);
  memcpy(g_sess.nonce_a, tlv->value, 16U);
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_ECDH_PUBLIC_KEY);
  memcpy(g_sess.app_ecdh_pubkey, tlv->value, 65U);

#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ nonceA=%s appEcdhPubKey=%s" USER_LOG_NL,
                hex_str(g_sess.nonce_a, 16U, 16U),
                hex_str(g_sess.app_ecdh_pubkey, 16U, 16U));
#endif

  /* 延迟 ECDH 密钥生成 (phone_session_begin_auth 内部 psa_generate_key 阻塞 ~400ms,
   * 拆分到独立主循环步骤 phone_sm_process_auth_deferred, 避免阻塞 CAN) */
  memcpy(g_auth_defer.stored_key_id, stored_key_id, 16U);
  g_auth_defer.seq    = seq;
  g_auth_defer.active = true;
  USER_LOG_INFO("[SM] AUTH_CHALLENGE_REQ seq=%u → ECDH gen deferred" USER_LOG_NL,
                (unsigned)seq);
  return;
}

/* ========================================================================== */
/* Cmd 0x21: AUTH_CHALLENGE_RSP (明文+ECDSA, 第 11.2 / 22.3 节)               */
/* ========================================================================== */

static void handle_auth_challenge_rsp(uint16_t seq,
                                      const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */
  
  /* ===== 第二步：必选TLV校验 ===== */
  /* 协议 §22.3 冻结版: AUTH_CHALLENGE_RSP Request = appKeyId + authSessionId + challengeId + appCounter + appSignature */
  /* (nonceA 和 appEcdhPublicKey 已在 AUTH_CHALLENGE_REQ 中传入) */
  const uint8_t req_tlvs[] = {PHONE_TLV_APP_KEY_ID, PHONE_TLV_AUTH_SESSION_ID,
                              PHONE_TLV_CHALLENGE_ID, PHONE_TLV_APP_COUNTER,
                              PHONE_TLV_APP_SIGNATURE};
  const phone_tlv_t *tlv;
  sl_status_t sc;
  uint64_t app_counter;
  uint8_t stored_pubkey[65];
  uint8_t stored_key_id[16];

  /* SILENT 状态拦截 (§22.5): 拒绝, 返回 DEVICE_SILENT + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, false, true, true);
    return;
  }

  if (!phone_session_is_auth_challenge_active(&g_sess)) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP seq=%u no active challenge (auth_active=%d, elapsed=%sms, timeout=%ums)" USER_LOG_NL,
                  (unsigned)seq, (int)g_sess.auth_challenge_active,
                  u64_dec_str(phone_session_now_ms() - (g_sess.auth_deadline_ms - PHONE_TIMEOUT_AUTH_CHALLENGE_MS)),
                  (unsigned)PHONE_TIMEOUT_AUTH_CHALLENGE_MS);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, false, true, false);
    return;
  }

  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }

  /* 校验 challengeId */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_CHALLENGE_ID);
  if (memcmp(tlv->value, g_sess.challenge_id, 16U) != 0) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP CHALLENGE_MISMATCH rcv=%s expected=%s" USER_LOG_NL,
                  hex_str(tlv->value, 16U, 16U), hex_str(g_sess.challenge_id, 16U, 16U));
    phone_session_record_security_failure(&g_sess);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_FAILED, false, true, false);
    return;
  }

  /* 校验 appCounter (received > stored, 允许跳号) */
  if (!phone_tlv_read_u64(tlvs, tlv_count, PHONE_TLV_APP_COUNTER, &app_counter)) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP APP_COUNTER_TLV_INVALID" USER_LOG_NL);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }
  if (app_counter <= phone_storage_get_app_counter()) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP AppCtr_REJECT got=%s stored=%s" USER_LOG_NL,
                  u64_dec_str(app_counter), u64_dec_str(phone_storage_get_app_counter()));
    phone_session_record_security_failure(&g_sess);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_FAILED, false, true, false);
    return;
  }

  /* nonceA 和 appEcdhPublicKey 已在 AUTH_CHALLENGE_REQ 中提取并保存到 g_sess
   * (协议 §22.3 冻结版: nonceA + appEcdhPublicKey 移至 AUTH_CHALLENGE_REQ Request) */

  /* 校验 appKeyId (必须匹配已绑定的) */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_KEY_ID);
  sc = phone_storage_get_app_key_id(stored_key_id);
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP appKeyId_check rcv=%s stored=%s" USER_LOG_NL,
                hex_str(tlv->value, 16U, 16U),
                (sc == SL_STATUS_OK) ? hex_str(stored_key_id, 16U, 16U) : "READ_FAIL");
#endif
  if (sc != SL_STATUS_OK || memcmp(tlv->value, stored_key_id, 16U) != 0) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP KEY_ID_MISMATCH" USER_LOG_NL);
    phone_session_record_security_failure(&g_sess);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_FAILED, false, true, false);
    return;
  }

  /* 获取已绑定 APP 公钥 (用于后续验签) */
  sc = phone_storage_get_app_public_key(stored_pubkey);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP PUBKEY_READ_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }
#ifdef PHONE_DUMP_FRAMES
  USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP 验签公钥(已绑定APP_PUBKEY): %s" USER_LOG_NL,
                hex_str_full(stored_pubkey, 65U));
#endif

  /* 延迟 ECDH 共享密钥计算 (phone_crypto_ecdh_compute_shared, PSA→SE, ~400ms)
   * ECDH + SHA-256 + ECDSA 全部在独立主循环步骤中完成, CAN 不受影响 */
  {
    const phone_tlv_t *tlv_sig = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_SIGNATURE);
    g_ecdh_rsp_defer.active      = true;
    g_ecdh_rsp_defer.seq         = seq;
    g_ecdh_rsp_defer.app_counter = app_counter;
    memcpy(g_ecdh_rsp_defer.pub_key, stored_pubkey, 65U);
    memcpy(g_ecdh_rsp_defer.sig, tlv_sig->value, 64U);
    USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP seq=%u appCounter=%s → ECDH deferred" USER_LOG_NL,
                  (unsigned)seq, u64_dec_str(app_counter));
  }
  return;
}

/* ========================================================================== */
/* V1.2: 车辆状态映射辅助函数                                                   */
/* ========================================================================== */

/** 底层 ignition gear → 协议 ignitionState (偏移+1, UNKNOWN=0x00) */
static uint8_t map_ignition_to_protocol(uint8_t raw)
{
  switch (raw) {
    case IGNITION_OFF:   return PHONE_IGNITION_STATE_OFF;
    case IGNITION_ACC:   return PHONE_IGNITION_STATE_ACC;
    case IGNITION_ON:    return PHONE_IGNITION_STATE_ON;
    case IGNITION_START: return PHONE_IGNITION_STATE_START;
    default:             return PHONE_IGNITION_STATE_UNKNOWN;
  }
}

/** 底层 4门 bitmask → 协议 doorState (左右简化) */
static uint8_t map_doors_to_protocol(uint8_t raw_bitmask)
{
  bool left  = (raw_bitmask & (DOOR_FL_MASK | DOOR_RL_MASK)) != 0U;
  bool right = (raw_bitmask & (DOOR_FR_MASK | DOOR_RR_MASK)) != 0U;
  if (left && right) return PHONE_DOOR_STATE_BOTH_OPEN;
  if (left)          return PHONE_DOOR_STATE_LEFT_OPEN;
  if (right)         return PHONE_DOOR_STATE_RIGHT_OPEN;
  return PHONE_DOOR_STATE_ALL_CLOSED;
}

/* ========================================================================== */
/* Cmd 0x03: GET_STATUS (sessionKey 加密, 第 11.4 / 22.3 节)                  */
/* ========================================================================== */

static void handle_get_status(uint16_t seq)
{
  USER_LOG_INFO("[SM] GET_STATUS seq=%u" USER_LOG_NL, (unsigned)seq);

  if (!phone_session_is_authenticated(&g_sess)) {
    USER_LOG_INFO("[SM] GET_STATUS NOT_AUTHENTICATED" USER_LOG_NL);
    send_error_response(PHONE_CMD_GET_STATUS, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }

  /* SILENT 状态拦截 (协议 §22.5): 终止会话, 返回 DEVICE_SILENT + remainSec, 然后断开 */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      uint64_t diff = (g_sess.silent_until_ms - now) / 1000ULL;
      remain = (diff > 65535ULL) ? 65535U : (uint16_t)diff;
    }

    USER_LOG_INFO("[SM] GET_STATUS SILENT remain=%u — 返回 DEVICE_SILENT + 断开" USER_LOG_NL,
                  (unsigned)remain);

    /* 返回 DEVICE_SILENT + remainSec (使用加密响应, 因为 sessionKey 仍然有效) */
    /* 协议 §22.3: 失败Response = result + errorCode + bindState + remainSec(可选) */
    {
      uint8_t payload[48];
      uint16_t plen;
      phone_tlv_t rsp_tlvs[4];
      uint8_t rcnt = 0U;
      uint8_t u8_r, u8_b;
      uint8_t ec_buf[2];
      uint8_t rs_buf[2];

      u8_r = PHONE_RESULT_FAIL;
      rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;

      phone_frame_write_u16_be(ec_buf, PHONE_ERR_DEVICE_SILENT);
      rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;

      u8_b = g_sess.device_state;
      rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

      phone_frame_write_u16_be(rs_buf, remain);
      rsp_tlvs[rcnt].type = PHONE_TLV_REMAIN_SEC; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = rs_buf; rcnt++;

      if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
        (void)send_encrypted_response(PHONE_CMD_GET_STATUS, seq, payload, plen, false);
      }
    }

    /* 断开连接 (协议要求: 返回 DEVICE_SILENT 后断开) */
    if (g_disc_fn != NULL) {
      g_disc_fn();
    }
    return;
  }

  /* 成功响应: V1.2 扩展为 7 个 TLV (新增 ignitionState/remainingRange/doorState) */
  {
    uint8_t payload[96];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[8];
    uint8_t rcnt = 0U;
    uint8_t u8_r, u8_b, u8_l, u8_ign, u8_door;
    uint8_t ec_buf[2] = {0, 0};
    uint8_t range_buf[2];
    sl_status_t tlv_sc;

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    u8_b = g_sess.device_state;
    rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

    u8_l = g_vehicle_lock_state;
    rsp_tlvs[rcnt].type = PHONE_TLV_VEHICLE_LOCK_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_l; rcnt++;

    /* V1.2 新增: 3 个车辆状态字段 */
    u8_ign = map_ignition_to_protocol(vehicle_state_get_ignition_gear());
    rsp_tlvs[rcnt].type = PHONE_TLV_IGNITION_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_ign; rcnt++;

    {
      uint16_t raw_range = vehicle_state_get_remaining_range_km();
      phone_frame_write_u16_be(range_buf, raw_range);
    }
    rsp_tlvs[rcnt].type = PHONE_TLV_REMAINING_RANGE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = range_buf; rcnt++;

    u8_door = map_doors_to_protocol(vehicle_state_get_door_status());
    rsp_tlvs[rcnt].type = PHONE_TLV_DOOR_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_door; rcnt++;

    USER_LOG_INFO("[SM] GET_STATUS seq=%u TLV count=%u bind=%u lock=%u ign=%u range=%u door=%u" USER_LOG_NL,
                  (unsigned)seq, (unsigned)rcnt,
                  (unsigned)u8_b, (unsigned)u8_l,
                  (unsigned)u8_ign, (unsigned)phone_frame_read_u16_be(range_buf),
                  (unsigned)u8_door);

    tlv_sc = phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload));
    USER_LOG_INFO("[SM] GET_STATUS seq=%u tlv_encode sc=0x%04lX plen=%u" USER_LOG_NL,
                  (unsigned)seq, (unsigned long)tlv_sc, (unsigned)plen);

    if (tlv_sc == SL_STATUS_OK) {
      USER_LOG_INFO("[SM] GET_STATUS seq=%u → send_encrypted_response(payload[%u])" USER_LOG_NL,
                    (unsigned)seq, (unsigned)plen);
      (void)send_encrypted_response(PHONE_CMD_GET_STATUS, seq, payload, plen, false);
    }
  }
}

/* ========================================================================== */
/* Cmd 0x30: REBIND_REQUEST (绑定加密帧, 第 9.2 / 22.3 节)                    */
/* ========================================================================== */

static void handle_rebind_request(uint16_t seq,
                                  const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */
  
  /* ===== 第二步：必选TLV校验 ===== */
  const uint8_t req_tlvs[] = {PHONE_TLV_BIND_SESSION_ID};
  sl_status_t sc;

  USER_LOG_INFO("[SM] REBIND_REQUEST seq=%u" USER_LOG_NL, (unsigned)seq);

  /* SILENT 状态拦截 (§22.5) */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] REBIND_REQUEST SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_REBIND_REQUEST, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, false, true, true);
    return;
  }

  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] REBIND_REQUEST TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_REBIND_REQUEST, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, true);
    return;
  }

  if (g_sess.device_state != PHONE_DEVICE_STATE_BOUND
      || !phone_session_is_bind_active(&g_sess)
      || !g_sess.qr_verified) {
    USER_LOG_INFO("[SM] REBIND_REQUEST STATE_CHECK_FAIL state=%u bind=%d qr=%d" USER_LOG_NL,
                  (unsigned)g_sess.device_state, (int)phone_session_is_bind_active(&g_sess),
                  (int)g_sess.qr_verified);
    send_error_response(PHONE_CMD_REBIND_REQUEST, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, false, true, true);
    return;
  }

  /* PEPS/车辆条件检查 (串口/外部可控) */
  if (!g_auth_condition_met) {
    USER_LOG_INFO("[SM] REBIND_REQUEST AUTH_COND_NOT_MET (g_auth_condition_met=false)" USER_LOG_NL);
    send_error_response(PHONE_CMD_REBIND_REQUEST, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_COND_NOT_MET, false, true, true);
    return;
  }

  /* V1.2: VIN 一致性校验 — 换绑前确认当前 VIN 与已保存 VIN 一致 */
  if (!vehicle_state_is_vin_available()) {
    USER_LOG_INFO("[SM] REBIND_REQUEST VIN_NOT_AVAILABLE" USER_LOG_NL);
    send_error_response(PHONE_CMD_REBIND_REQUEST, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_VEHICLE_ASSOCIATION_FAILED, false, true, true);
    return;
  }
  if (user_vin_get_abstract_status() != VIN_ASSOC_NORMAL) {
    USER_LOG_INFO("[SM] REBIND_REQUEST VIN_MISMATCH status=%u" USER_LOG_NL,
                  (unsigned)user_vin_get_abstract_status());
    send_error_response(PHONE_CMD_REBIND_REQUEST, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_VEHICLE_ASSOCIATION_FAILED, false, true, true);
    return;
  }

  /* 进入换绑窗口 */
  g_sess.device_state = PHONE_DEVICE_STATE_REBIND_WINDOW;
  phone_storage_set_bind_state(PHONE_DEVICE_STATE_REBIND_WINDOW);

  /* 成功响应 */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[4];
    uint8_t rcnt = 0U;
    uint8_t u8_r, u8_b;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    /* remainSec (使用本地变量) */
    uint8_t rs_buf[2];
    uint64_t remain = 0ULL;
    if (g_sess.bind_deadline_ms > phone_session_now_ms()) {
      remain = (g_sess.bind_deadline_ms - phone_session_now_ms()) / 1000ULL;
    }
    phone_frame_write_u16_be(rs_buf, (uint16_t)remain);
    rsp_tlvs[rcnt].type = PHONE_TLV_REMAIN_SEC; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = rs_buf; rcnt++;

    u8_b = g_sess.device_state;
    rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_REBIND_REQUEST, seq, payload, plen, true);
    }
  }
}

/* ========================================================================== */
/* Cmd 0x31: REBIND_REGISTER_KEY (绑定加密帧, 第 11.1 / 22.3 节)              */
/* ========================================================================== */

static void handle_rebind_register_key(uint16_t seq,
                                       const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */
  
  const uint8_t req_tlvs[] = {PHONE_TLV_BIND_SESSION_ID,
                              PHONE_TLV_APP_PUBLIC_KEY, PHONE_TLV_PUBLIC_KEY_ALG};
  const phone_tlv_t *tlv;
  sl_status_t sc;
  uint8_t pubkey[65];
  uint8_t key_id[16];
  uint32_t new_ver;

  USER_LOG_INFO("[SM] REBIND_REGISTER_KEY seq=%u" USER_LOG_NL, (unsigned)seq);

  /* SILENT 状态拦截 (§22.5) */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] REBIND_REGISTER_KEY SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, false, true, true);
    return;
  }

  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] REBIND_REGISTER_KEY TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, false, true, false);
    return;
  }

  /* 校验 publicKeyAlg (必须为 0x01 = ECDSA_P256_SHA256) */
  {
    uint8_t alg;
    phone_tlv_read_u8(tlvs, tlv_count, PHONE_TLV_PUBLIC_KEY_ALG, &alg);
    if (alg != PHONE_PUBKEY_ALG_ECDSA_P256_SHA256) {
      USER_LOG_INFO("[SM] REBIND_REGISTER_KEY PUBKEY_ALG_INVALID alg=0x%02X" USER_LOG_NL, (unsigned)alg);
      send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PARAM_INVALID, false, true, false);
      return;
    }
  }

  if (g_sess.device_state != PHONE_DEVICE_STATE_REBIND_WINDOW
      || !phone_session_is_bind_active(&g_sess)
      || !g_sess.qr_verified) {
    USER_LOG_INFO("[SM] REBIND_REGISTER_KEY STATE_CHECK_FAIL state=%u bind=%d qr=%d" USER_LOG_NL,
                  (unsigned)g_sess.device_state, (int)phone_session_is_bind_active(&g_sess),
                  (int)g_sess.qr_verified);
    send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, false, true, false);
    return;
  }

  /* V1.2: VIN 再次确认 — 提交前确认 VIN 状态正常 (换绑窗口期间 VIN 可能变化) */
  if (!vehicle_state_is_vin_available()
      || user_vin_get_abstract_status() != VIN_ASSOC_NORMAL) {
    USER_LOG_INFO("[SM] REBIND_REGISTER_KEY VIN_CHECK_FAIL avail=%d status=%u" USER_LOG_NL,
                  (int)vehicle_state_is_vin_available(),
                  (unsigned)user_vin_get_abstract_status());
    send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_VEHICLE_ASSOCIATION_FAILED, false, true, false);
    return;
  }

  /* 提取新公钥 */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_PUBLIC_KEY);
  memcpy(pubkey, tlv->value, 65U);

  /* 生成新 appKeyId */
  sc = phone_crypto_get_random(key_id, 16U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] REBIND_REGISTER_KEY TRNG_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }

  {
    uint32_t cur_ver = phone_storage_get_bind_version();
    if (cur_ver == 0xFFFFFFFFU) {
      USER_LOG_INFO("[SM] REBIND_REGISTER_KEY BIND_VERSION_OVERFLOW cur=%lu — 拒绝, 需服务处理" USER_LOG_NL,
                    (unsigned long)cur_ver);
      send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_INTERNAL_ERROR, false, true, false);
      return;
    }
    new_ver = cur_ver + 1U;
  }

  /* 原子换绑 (V1.2: NULL=VIN 不修改, 保留已保存 VIN) */
  sc = phone_storage_atomic_rebind(pubkey, key_id, new_ver);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] REBIND_REGISTER_KEY ATOMIC_WRITE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, false, true, false);
    return;
  }

  /* 更新设备状态，但先不结束绑定会话 — send_encrypted_response 还需要 bind_session_key */
  g_sess.device_state = PHONE_DEVICE_STATE_BOUND;
  phone_storage_set_bind_state(PHONE_DEVICE_STATE_BOUND);

  USER_LOG_INFO("[SM] REBIND_REGISTER_KEY OK newVer=%lu" USER_LOG_NL, (unsigned long)new_ver);

  /* 成功响应 */
  {
    uint8_t payload[128];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[6];
    uint8_t rcnt = 0U;
    uint8_t u8_r, u8_b;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_APP_KEY_ID; rsp_tlvs[rcnt].len = 16U; rsp_tlvs[rcnt].value = key_id; rcnt++;

    /* bindVersion (使用本地变量) */
    uint8_t bv_buf2[4];
    phone_frame_write_u32_be(bv_buf2, new_ver);
    rsp_tlvs[rcnt].type = PHONE_TLV_BIND_VERSION; rsp_tlvs[rcnt].len = 4U; rsp_tlvs[rcnt].value = bv_buf2; rcnt++;

    u8_b = PHONE_DEVICE_STATE_BOUND;
    rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_REBIND_REGISTER_KEY, seq, payload, plen, true);
    }
  }

  /* 加密响应发送完成后才清除绑定会话 (否则 encrypt 拿到的是已清零的 key) */
  phone_session_end_bind(&g_sess);

  /* V1.2 23.10: 换绑成功后清理旧无感状态 + 旧 Bond
   *   - 清 passiveEnabled/passiveAuthorized、hidRuntime、quota
   *   - passiveSensitivity 恢复 Standard (无查询协议, 保证 APP/BG24 档位一致)
   *   - 删除旧系统 Bond (换绑视为新手机身份; 旧 Bond 无无感权限, 需重新 Pair) */
  g_sess.passive_enabled = false;
  g_sess.hid_runtime_enabled = false;
  hid_service_set_runtime_enabled(false);
  (void)phone_storage_set_passive_enabled(false);

  g_sess.passive_sensitivity = PHONE_PASSIVE_SENS_STANDARD;
  (void)phone_storage_set_passive_sensitivity(PHONE_PASSIVE_SENS_STANDARD);

  g_sess.passive_quota_remaining = PHONE_PASSIVE_QUOTA_DEFAULT;
  (void)phone_storage_set_passive_quota(g_sess.passive_quota_remaining);

  (void)sl_bt_sm_delete_bondings();
  USER_LOG_INFO("[SM] REBIND: cleared passive state + deleted old bonds (23.10)" USER_LOG_NL);
}

/* ========================================================================== */
/* Cmd 0x40: CTRL_CHALLENGE_REQ (sessionKey 加密, 第 11.3 / 22.3 节)         */
/* ========================================================================== */

static void handle_ctrl_challenge_req(uint16_t seq)
{
  sl_status_t sc;

  USER_LOG_INFO("[SM] CTRL_CHALLENGE_REQ seq=%u" USER_LOG_NL, (unsigned)seq);

  /* SILENT 状态拦截 (§22.5): 拒绝, 返回 DEVICE_SILENT + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      remain = (uint16_t)((g_sess.silent_until_ms - now) / 1000ULL);
    }
    USER_LOG_INFO("[SM] CTRL_CHALLENGE_REQ SILENT remain=%u" USER_LOG_NL, (unsigned)remain);
    send_error_response(PHONE_CMD_CTRL_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, true, true, true);
    return;
  }

  if (!phone_session_is_authenticated(&g_sess)) {
    USER_LOG_INFO("[SM] CTRL_CHALLENGE_REQ NOT_AUTHENTICATED" USER_LOG_NL);
    send_error_response(PHONE_CMD_CTRL_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }

  sc = phone_session_begin_ctrl_challenge(&g_sess);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] CTRL_CHALLENGE_REQ BEGIN_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_CTRL_CHALLENGE_REQ, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, true, true, false);
    return;
  }

  /* 延迟响应发送 (send_encrypted_response 耗时 ~500ms, CAN 优先) */
  g_cmd_deferred = true;
  g_ctrl_challenge_defer.seq    = seq;
  g_ctrl_challenge_defer.active = true;
  USER_LOG_INFO("[SM] CTRL_CHALLENGE_REQ seq=%u → response deferred" USER_LOG_NL,
                (unsigned)seq);
  return;
}

/* ========================================================================== */
/* Cmd 0x41: CTRL_COMMAND (sessionKey+AES-CCM+ECDSA, 第 11.3 / 22.3 节)      */
/* ========================================================================== */

static void handle_ctrl_command(uint16_t seq,
                                const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  /* TLV 打印已集中于 process_received_frame */
  
  /* ===== 第二步：必选TLV校验 ===== */
  const uint8_t req_tlvs[] = {PHONE_TLV_APP_KEY_ID, PHONE_TLV_AUTH_SESSION_ID,
                              PHONE_TLV_CHALLENGE_ID, PHONE_TLV_APP_COUNTER,
                              PHONE_TLV_CONTROL_CMD, PHONE_TLV_CONTROL_FLAGS,
                              PHONE_TLV_APP_SIGNATURE};
  const phone_tlv_t *tlv;
  sl_status_t sc;
  uint64_t app_counter;
  uint8_t control_cmd;
  uint8_t payload[256];
  uint16_t plen;

  if (!phone_session_is_authenticated(&g_sess)
      || !phone_session_is_ctrl_challenge_active(&g_sess)) {
    USER_LOG_INFO("[SM] CTRL_COMMAND SESSION_CHECK_FAIL auth=%d ctrl_active=%d" USER_LOG_NL,
                  (int)phone_session_is_authenticated(&g_sess),
                  (int)phone_session_is_ctrl_challenge_active(&g_sess));
    send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }

  /* SILENT 状态拦截 (§22.5 + §22.3 失败Response): result + errorCode + controlCmd(UNKNOWN) + vehicleLockState(UNKNOWN) + bindState + remainSec */
  if (g_sess.is_silent) {
    uint64_t now = phone_session_now_ms();
    uint16_t remain = 0U;
    if (g_sess.silent_until_ms > now) {
      uint64_t diff = (g_sess.silent_until_ms - now) / 1000ULL;
      remain = (diff > 65535ULL) ? 65535U : (uint16_t)diff;
    }
    USER_LOG_INFO("[SM] CTRL_COMMAND SILENT remain=%u" USER_LOG_NL, (unsigned)remain);

    {
      uint8_t sil_payload[48];
      uint16_t sil_plen;
      phone_tlv_t sil_tlvs[6];
      uint8_t sil_cnt = 0U;
      uint8_t sil_r, sil_b, sil_c, sil_l;
      uint8_t sil_ec[2];
      uint8_t sil_rs[2];

      sil_r = PHONE_RESULT_FAIL;
      sil_tlvs[sil_cnt].type = PHONE_TLV_RESULT; sil_tlvs[sil_cnt].len = 1U; sil_tlvs[sil_cnt].value = &sil_r; sil_cnt++;

      phone_frame_write_u16_be(sil_ec, PHONE_ERR_DEVICE_SILENT);
      sil_tlvs[sil_cnt].type = PHONE_TLV_ERROR_CODE; sil_tlvs[sil_cnt].len = 2U; sil_tlvs[sil_cnt].value = sil_ec; sil_cnt++;

      sil_c = 0x00;  /* controlCmd=UNKNOWN */
      sil_tlvs[sil_cnt].type = PHONE_TLV_CONTROL_CMD; sil_tlvs[sil_cnt].len = 1U; sil_tlvs[sil_cnt].value = &sil_c; sil_cnt++;

      sil_l = PHONE_LOCK_STATE_UNKNOWN;
      sil_tlvs[sil_cnt].type = PHONE_TLV_VEHICLE_LOCK_STATE; sil_tlvs[sil_cnt].len = 1U; sil_tlvs[sil_cnt].value = &sil_l; sil_cnt++;

      sil_b = g_sess.device_state;
      sil_tlvs[sil_cnt].type = PHONE_TLV_BIND_STATE; sil_tlvs[sil_cnt].len = 1U; sil_tlvs[sil_cnt].value = &sil_b; sil_cnt++;

      phone_frame_write_u16_be(sil_rs, remain);
      sil_tlvs[sil_cnt].type = PHONE_TLV_REMAIN_SEC; sil_tlvs[sil_cnt].len = 2U; sil_tlvs[sil_cnt].value = sil_rs; sil_cnt++;

      if (phone_tlv_encode(sil_tlvs, sil_cnt, sil_payload, &sil_plen, sizeof(sil_payload)) == SL_STATUS_OK) {
        (void)send_encrypted_response(PHONE_CMD_CTRL_COMMAND, seq, sil_payload, sil_plen, false);
      }
    }
    return;
  }

  sc = phone_tlv_validate_required(tlvs, tlv_count, req_tlvs,
                                   sizeof(req_tlvs) / sizeof(req_tlvs[0]));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] CTRL_COMMAND TLV_VALIDATE_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, true, true, false);
    return;
  }

  /* 校验 challengeId */
  tlv = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_CHALLENGE_ID);
  if (memcmp(tlv->value, g_sess.ctrl_challenge_id, 16U) != 0) {
    USER_LOG_INFO("[SM] CTRL_COMMAND CHALLENGE_MISMATCH rcv=%s expected=%s" USER_LOG_NL,
                  hex_str(tlv->value, 16U, 16U), hex_str(g_sess.ctrl_challenge_id, 16U, 16U));
    phone_session_record_security_failure(&g_sess);
    send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_FAILED, true, true, false);
    return;
  }

  /* 校验 appCounter */
  if (!phone_tlv_read_u64(tlvs, tlv_count, PHONE_TLV_APP_COUNTER, &app_counter)) {
    USER_LOG_INFO("[SM] CTRL_COMMAND APP_COUNTER_TLV_INVALID" USER_LOG_NL);
    send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, true, true, false);
    return;
  }
  if (app_counter <= phone_storage_get_app_counter()) {
    USER_LOG_INFO("[SM] CTRL_COMMAND AppCtr_REJECT got=%s stored=%s" USER_LOG_NL,
                  u64_dec_str(app_counter), u64_dec_str(phone_storage_get_app_counter()));
    phone_session_record_security_failure(&g_sess);
    send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_AUTH_FAILED, true, true, false);
    return;
  }

  /* 提取 controlCmd */
  if (!phone_tlv_read_u8(tlvs, tlv_count, PHONE_TLV_CONTROL_CMD, &control_cmd)) {
    USER_LOG_INFO("[SM] CTRL_COMMAND CONTROL_CMD_TLV_INVALID" USER_LOG_NL);
    send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PARAM_INVALID, true, true, false);
    return;
  }

  /* 构造 CTRL_SIGN_BYTES 并验签 (协议第 10.2 节) */
  {
    uint8_t sign_buf[512];
    uint16_t sign_len = 0U;
    uint8_t digest[32];
    const char *prefix = "BLEKEY-CTRL-V1";
    uint8_t u8_r, u8_b;
    uint16_t u16;
    char did[PHONE_DEVICE_ID_MAX_LEN + 1];
    uint8_t stored_key_id[16];
    uint8_t stored_pubkey[65];  /* 添加公钥缓冲区 */
    const phone_tlv_t *tlv_app_sig;
    const phone_tlv_t *tlv_cmd_param;
    uint8_t ctrl_flags;
    sl_status_t sc;  /* 添加状态码变量 */
    
    memset(did, 0, sizeof(did));
    phone_storage_get_device_id(did, sizeof(did) - 1U);
    phone_storage_get_app_key_id(stored_key_id);
    
    /* 获取APP公钥用于验签 */
    sc = phone_storage_get_app_public_key(stored_pubkey);
    if (sc != SL_STATUS_OK) {
      USER_LOG_INFO("[SM] CTRL_COMMAND GET_PUBKEY_FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
      send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_AUTH_FAILED, true, true, false);
      return;
    }
    
    /* ASCII("BLEKEY-CTRL-V1") */
    memcpy(&sign_buf[sign_len], prefix, 14U);
    sign_len += 14U;
    
    /* U8(protocolVersion) */
    sign_buf[sign_len++] = PHONE_FRAME_VERSION;
    
    /* U16_BE(deviceIdLen) | deviceId */
    u16 = (uint16_t)strlen(did);
    sign_buf[sign_len++] = (u16 >> 8) & 0xFF;
    sign_buf[sign_len++] = u16 & 0xFF;
    memcpy(&sign_buf[sign_len], did, u16);
    sign_len += u16;
    
    /* U16_BE(appKeyIdLen) | appKeyId */
    sign_buf[sign_len++] = 0x00;
    sign_buf[sign_len++] = 0x10;  /* 16 bytes */
    memcpy(&sign_buf[sign_len], stored_key_id, 16U);
    sign_len += 16U;
    
    /* authSessionId (16) */
    memcpy(&sign_buf[sign_len], g_sess.auth_session_id, 16U);
    sign_len += 16U;
    
    /* challengeId (16) */
    memcpy(&sign_buf[sign_len], g_sess.ctrl_challenge_id, 16U);
    sign_len += 16U;
    
    /* nonce (16) - 控制挑战的 nonce */
    memcpy(&sign_buf[sign_len], g_sess.ctrl_nonce, 16U);
    sign_len += 16U;
    
    /* U64_BE(appCounter) */
    tlv_app_sig = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_COUNTER);
    for (int i = 0; i < 8; i++) {
      sign_buf[sign_len++] = tlv_app_sig->value[i];
    }
    
    /* U8(controlCmd) */
    sign_buf[sign_len++] = control_cmd;
    
    /* U16_BE(cmdParamLen) | cmdParam */
    tlv_cmd_param = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_CMD_PARAM);
    if (tlv_cmd_param != NULL) {
      u16 = tlv_cmd_param->len;
    } else {
      u16 = 0U;
    }
    sign_buf[sign_len++] = (u16 >> 8) & 0xFF;
    sign_buf[sign_len++] = u16 & 0xFF;
    if (tlv_cmd_param != NULL && u16 > 0) {
      memcpy(&sign_buf[sign_len], tlv_cmd_param->value, u16);
      sign_len += u16;
    }
    
    /* U8(controlFlags) */
    if (!phone_tlv_read_u8(tlvs, tlv_count, PHONE_TLV_CONTROL_FLAGS, &ctrl_flags)) {
      USER_LOG_INFO("[SM] CTRL_COMMAND CONTROL_FLAGS_TLV_INVALID" USER_LOG_NL);
      send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PARAM_INVALID, true, true, false);
      return;
    }
    sign_buf[sign_len++] = ctrl_flags;
    
    /* SHA-256(CTRL_SIGN_BYTES) */
    sc = phone_crypto_sha256(sign_buf, sign_len, digest);
    if (sc != SL_STATUS_OK) {
      USER_LOG_INFO("[SM] CTRL_SIGN_BYTES SHA256 FAIL sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
      phone_session_record_security_failure(&g_sess);
      send_error_response(PHONE_CMD_CTRL_COMMAND, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_AUTH_FAILED, true, true, false);
      return;
    }
    
    /* ECDSA verify (延迟执行以避免长时间阻塞主循环, CAN 可在等待期间运行) */
    tlv_app_sig = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_APP_SIGNATURE);
    g_cmd_deferred = true;
    g_ecdsa_defer.active       = true;
    g_ecdsa_defer.cmd    = PHONE_CMD_CTRL_COMMAND;
    g_ecdsa_defer.seq    = seq;
    memcpy(g_ecdsa_defer.pub_key, stored_pubkey, 65U);
    memcpy(g_ecdsa_defer.digest, digest, 32U);
    memcpy(g_ecdsa_defer.sig, tlv_app_sig->value, 64U);
    g_ecdsa_defer.app_counter = app_counter;
    g_ecdsa_defer.control_cmd = control_cmd;
    USER_LOG_INFO("[SM] CTRL_COMMAND seq=%u cmd=0x%02X counter=%s → ECDSA deferred" USER_LOG_NL,
                  (unsigned)seq, (unsigned)control_cmd, u64_dec_str(app_counter));
    /* 提前返回: ECDSA 验签 + 后处理由 phone_sm_deferred_ecdsa_poll 在下一轮主循环完成 */
    return;
  }

  /* unreachable (ECDSA 已延迟) */
}

/* ========================================================================== */
/* 延迟 ECDSA 验签结果处理                                                     */
/* ========================================================================== */

/** 在 phone_sm_process_action 中调用, 处理上一轮延迟的 ECDSA 验签结果 */
static void phone_sm_deferred_ecdsa_poll(void)
{
  sl_status_t sc;

  if (!g_ecdsa_defer.active) return;

  /* ★ ECDSA P-256 验签 (PSA → SE 硬件, 阻塞 ~80ms, 已验证成功)
   * CAN 已在本轮迭代开头运行, 80ms < 100ms CAN 周期, 不影响 CAN TX */
  sc = phone_crypto_ecdsa_verify(g_ecdsa_defer.pub_key,
                                  g_ecdsa_defer.digest,
                                  g_ecdsa_defer.sig);
  g_ecdsa_defer.active = false;

  if (sc != SL_STATUS_OK) {
    /* 验签失败: 统一记录安全失败 */
    USER_LOG_INFO("[SM] DEFERRED_ECDSA_FAIL cmd=0x%02X seq=%u sc=0x%04lX" USER_LOG_NL,
                  (unsigned)g_ecdsa_defer.cmd, (unsigned)g_ecdsa_defer.seq,
                  (unsigned long)sc);
    phone_session_record_security_failure(&g_sess);
    if (g_ecdsa_defer.cmd == PHONE_CMD_CTRL_COMMAND
        || g_ecdsa_defer.cmd == PHONE_CMD_AUTH_CHALLENGE_RSP
        || g_ecdsa_defer.cmd == PHONE_CMD_QR_VERIFY) {
      send_error_response(g_ecdsa_defer.cmd, g_ecdsa_defer.seq,
                          PHONE_RESULT_FAIL, PHONE_ERR_AUTH_FAILED,
                          true, true,
                          (g_ecdsa_defer.cmd == PHONE_CMD_QR_VERIFY));  /* remainSec 仅 QR_VERIFY 需要 */
    }
    return;
  }

  /* 验签成功: 根据命令类型执行后处理 */
  switch (g_ecdsa_defer.cmd) {

    case PHONE_CMD_CTRL_COMMAND: {
      uint8_t control_cmd = g_ecdsa_defer.control_cmd;
      uint64_t app_counter = g_ecdsa_defer.app_counter;

      USER_LOG_INFO("[SM] CTRL_COMMAND seq=%u cmd=0x%02X counter=%s ECDSA_VERIFY_OK (deferred)" USER_LOG_NL,
                    (unsigned)g_ecdsa_defer.seq, (unsigned)control_cmd,
                    u64_dec_str(app_counter));

      /* V1.2: 控制前 VIN 校验 — 安全校验通过后、向 VIU 下发前
       * VIN 无效/不一致 → STATE_NOT_ALLOWED (0x0002), 不计入安全失败, 不下发控制 */
      if (!vehicle_state_is_vin_available()
          || user_vin_get_abstract_status() != VIN_ASSOC_NORMAL) {
        USER_LOG_INFO("[SM] CTRL_COMMAND VIN_CHECK_FAIL avail=%d status=%u — 拒绝下发控制" USER_LOG_NL,
                      (int)vehicle_state_is_vin_available(),
                      (unsigned)user_vin_get_abstract_status());
        /* VIN 失败不计入安全失败 (§14: VIN校验返回的STATE_NOT_ALLOWED不计入) */
        {
          uint8_t vin_fail_payload[64];
          uint16_t vin_fail_plen;
          phone_tlv_t vin_tlvs[5];
          uint8_t vin_cnt = 0U;
          uint8_t vr, vb, vc, vl;
          uint8_t vec[2];

          vr = PHONE_RESULT_FAIL;
          vin_tlvs[vin_cnt].type = PHONE_TLV_RESULT; vin_tlvs[vin_cnt].len = 1U; vin_tlvs[vin_cnt].value = &vr; vin_cnt++;

          phone_frame_write_u16_be(vec, PHONE_ERR_STATE_NOT_ALLOWED);
          vin_tlvs[vin_cnt].type = PHONE_TLV_ERROR_CODE; vin_tlvs[vin_cnt].len = 2U; vin_tlvs[vin_cnt].value = vec; vin_cnt++;

          vc = control_cmd;
          vin_tlvs[vin_cnt].type = PHONE_TLV_CONTROL_CMD; vin_tlvs[vin_cnt].len = 1U; vin_tlvs[vin_cnt].value = &vc; vin_cnt++;

          vl = g_vehicle_lock_state;  /* 当前已知值或 UNKNOWN */
          vin_tlvs[vin_cnt].type = PHONE_TLV_VEHICLE_LOCK_STATE; vin_tlvs[vin_cnt].len = 1U; vin_tlvs[vin_cnt].value = &vl; vin_cnt++;

          vb = g_sess.device_state;
          vin_tlvs[vin_cnt].type = PHONE_TLV_BIND_STATE; vin_tlvs[vin_cnt].len = 1U; vin_tlvs[vin_cnt].value = &vb; vin_cnt++;

          if (phone_tlv_encode(vin_tlvs, vin_cnt, vin_fail_payload, &vin_fail_plen, sizeof(vin_fail_payload)) == SL_STATUS_OK) {
            (void)send_encrypted_response(PHONE_CMD_CTRL_COMMAND, g_ecdsa_defer.seq,
                                           vin_fail_payload, vin_fail_plen, false);
          }
        }
        /* 不消耗 challenge, 不下发控制 */
        return;
      }

      /* 消耗 control challenge */
      phone_session_consume_ctrl_challenge(&g_sess);
      /* 提交 appCounter */
      phone_storage_commit_app_counter(app_counter);
      /* 缓存远程命令 (供 CAN 桥接消费) */
      g_pending_control_cmd = control_cmd;

      /* 发送加密响应 */
      {
        uint8_t payload[256];
        uint16_t plen;
        phone_tlv_t rsp_tlvs[6];
        uint8_t rcnt = 0U;
        uint8_t u8_r, u8_b, u8_c, u8_l;
        uint8_t ec_buf[2] = {0, 0};

        u8_r = PHONE_RESULT_OK;
        rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;
        rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;
        u8_c = control_cmd;
        rsp_tlvs[rcnt].type = PHONE_TLV_CONTROL_CMD; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_c; rcnt++;
        u8_l = g_vehicle_lock_state;
        rsp_tlvs[rcnt].type = PHONE_TLV_VEHICLE_LOCK_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_l; rcnt++;
        u8_b = g_sess.device_state;
        rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

        if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
          (void)send_encrypted_response(PHONE_CMD_CTRL_COMMAND, g_ecdsa_defer.seq,
                                         payload, plen, false);
        }
      }
      break;
    }

    case PHONE_CMD_AUTH_CHALLENGE_RSP: {
      uint64_t app_counter = g_ecdsa_defer.app_counter;
      uint8_t session_key[16];
      uint8_t stored_key_id[16];
      char did[PHONE_DEVICE_ID_MAX_LEN + 1];

      USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP seq=%u appCounter=%s ECDSA_VERIFY_OK (deferred)" USER_LOG_NL,
                    (unsigned)g_ecdsa_defer.seq, u64_dec_str(app_counter));

      /* 派生 sessionKey (使用 g_ecdsa_defer.shared_secret) */
      memset(did, 0, sizeof(did));
      phone_storage_get_device_id(did, sizeof(did) - 1U);
      phone_storage_get_app_key_id(stored_key_id);

      sc = phone_crypto_derive_session_key(g_ecdsa_defer.shared_secret,
                                            g_sess.nonce_a, g_sess.nonce_b,
                                            did, stored_key_id, g_sess.auth_session_id,
                                            session_key);
      phone_crypto_memzero(g_ecdsa_defer.shared_secret, 32U);

      if (sc != SL_STATUS_OK) {
        USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP SESSION_KEY_DERIVE_FAIL (deferred) sc=0x%04lX" USER_LOG_NL,
                      (unsigned long)sc);
        send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, g_ecdsa_defer.seq,
                            PHONE_RESULT_FAIL, PHONE_ERR_INTERNAL_ERROR, false, true, false);
        return;
      }

      /* 完成认证 */
      phone_session_complete_auth(&g_sess, session_key);
      phone_crypto_memzero(session_key, 16U);
      phone_storage_commit_app_counter(app_counter);
      phone_session_clear_silent(&g_sess);

      USER_LOG_INFO("[SM] AUTH_CHALLENGE_RSP seq=%u AUTH_OK (deferred)" USER_LOG_NL,
                    (unsigned)g_ecdsa_defer.seq);

      /* 发送成功响应 */
      {
        uint8_t payload[64];
        uint16_t plen;
        phone_tlv_t rsp_tlvs[5];
        uint8_t rcnt = 0U;
        uint8_t u8_r, u8_b;
        uint8_t ec_buf[2] = {0, 0};
        uint8_t caps_buf[4];
        uint32_t u32;

        u8_r = PHONE_RESULT_OK;
        rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;
        rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;
        u8_b = g_sess.device_state;
        rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;
        u32 = PHONE_CAP_DEFAULT_V11;
        phone_frame_write_u32_be(caps_buf, u32);
        rsp_tlvs[rcnt].type = PHONE_TLV_CAPABILITY_FLAGS; rsp_tlvs[rcnt].len = 4U; rsp_tlvs[rcnt].value = caps_buf; rcnt++;

        if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
          (void)send_plain_response(PHONE_CMD_AUTH_CHALLENGE_RSP, g_ecdsa_defer.seq,
                                     payload, plen);
        }
      }
      break;
    }

    case PHONE_CMD_QR_VERIFY: {
      /* 标记 QR verified */
      phone_session_set_qr_verified(&g_sess);

      /* 校验 tokenBody 字段 */
      {
        char did_buf[PHONE_DEVICE_ID_MAX_LEN + 1];
        char qid_buf[PHONE_TLV_MAX_QID + 1];
        const char *tb_str = (const char *)g_ecdsa_defer.token_body;
        uint16_t tb_len = g_ecdsa_defer.token_body_len;

        memset(did_buf, 0, sizeof(did_buf));
        memset(qid_buf, 0, sizeof(qid_buf));
        phone_storage_get_device_id(did_buf, sizeof(did_buf) - 1U);
        phone_storage_get_qid(qid_buf, sizeof(qid_buf));

        if (tb_len < 20U || strstr(tb_str, "\"BIND\"") == NULL) {
          USER_LOG_INFO("[SM] QR_DEFERRED TOKEN_TYPE_MISMATCH" USER_LOG_NL);
          phone_session_record_security_failure(&g_sess);
          send_error_response(PHONE_CMD_QR_VERIFY, g_ecdsa_defer.seq,
                              PHONE_RESULT_FAIL, PHONE_ERR_AUTH_FAILED, false, true, true);
          break;
        }
        if (did_buf[0] != '\0' && strstr(tb_str, did_buf) == NULL) {
          USER_LOG_INFO("[SM] QR_DEFERRED TOKEN_DID_MISMATCH dev='%s'" USER_LOG_NL, did_buf);
          phone_session_record_security_failure(&g_sess);
          send_error_response(PHONE_CMD_QR_VERIFY, g_ecdsa_defer.seq,
                              PHONE_RESULT_FAIL, PHONE_ERR_AUTH_FAILED, false, true, true);
          break;
        }
        if (qid_buf[0] != '\0' && strstr(tb_str, qid_buf) == NULL) {
          USER_LOG_INFO("[SM] QR_DEFERRED TOKEN_QID_MISMATCH qid='%s'" USER_LOG_NL, qid_buf);
          phone_session_record_security_failure(&g_sess);
          send_error_response(PHONE_CMD_QR_VERIFY, g_ecdsa_defer.seq,
                              PHONE_RESULT_FAIL, PHONE_ERR_AUTH_FAILED, false, true, true);
          break;
        }
      }

      USER_LOG_INFO("[SM] QR_DEFERRED OK seq=%u" USER_LOG_NL,
                    (unsigned)g_ecdsa_defer.seq);

      /* 成功响应: result + errorCode + bindState + remainSec */
      {
        uint8_t payload[64];
        uint16_t plen;
        phone_tlv_t rsp_tlvs[4];
        uint8_t rcnt = 0U;
        uint8_t u8_r, u8_b;
        uint8_t ec_buf[2] = {0, 0};
        uint8_t rs_buf[2];

        u8_r = PHONE_RESULT_OK;
        rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;
        rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;
        u8_b = g_sess.device_state;
        rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;
        {
          uint64_t rem = 0ULL;
          if (g_sess.bind_deadline_ms > phone_session_now_ms()) {
            rem = (g_sess.bind_deadline_ms - phone_session_now_ms()) / 1000ULL;
          }
          phone_frame_write_u16_be(rs_buf, (uint16_t)rem);
          rsp_tlvs[rcnt].type = PHONE_TLV_REMAIN_SEC; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = rs_buf; rcnt++;
        }
        if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
          //(void)send_plain_response(PHONE_CMD_QR_VERIFY, g_ecdsa_defer.seq, payload, plen);
          //Loh: QR_VERIFY 属绑定加密帧, 成功响应须用 bind_session_key 加密(与 0x11/0x12 一致); 发明文 APP 会报 EncryptedSessionException 并断开
          (void)send_encrypted_response(PHONE_CMD_QR_VERIFY, g_ecdsa_defer.seq, payload, plen, true);
        }
      }
      break;
    }

    default:
      USER_LOG_INFO("[SM] DEFERRED_ECDSA_UNKNOWN_CMD 0x%02X" USER_LOG_NL,
                    (unsigned)g_ecdsa_defer.cmd);
      break;
  }
}

/* ========================================================================== */
/* 延迟 AUTH_CHALLENGE_REQ: ECDH 密钥生成                                        */
/* ========================================================================== */

/** 在 phone_sm_process_action 中调用, 完成延迟的 ECDH 密钥生成并发送响应 */
static void phone_sm_process_auth_deferred(void)
{
  sl_status_t sc;
  uint8_t payload[256];
  uint16_t plen;
  phone_tlv_t rsp_tlvs[8];
  uint8_t rcnt;
  uint8_t u8_r, u8_b;
  uint8_t ec_buf[2] = {0, 0};

  if (!g_auth_defer.active) return;
  if (g_ecdsa_defer.active)  return;  /* ECDSA 先完成, 避免 SE 锁竞争 */

  /* ★ ECDH 密钥生成 (阻塞 ~400ms, 但 CAN 已在本次迭代中运行) ★ */
  sc = phone_session_begin_auth(&g_sess, g_auth_defer.stored_key_id);
  g_auth_defer.active = false;

  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] AUTH_DEFERRED BEGIN_AUTH_FAIL sc=0x%04lX" USER_LOG_NL,
                  (unsigned long)sc);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_REQ, g_auth_defer.seq,
                        PHONE_RESULT_FAIL, PHONE_ERR_INTERNAL_ERROR,
                        false, true, false);
    return;
  }

  USER_LOG_INFO("[SM] AUTH_DEFERRED seq=%u ECDH gen done, sending response" USER_LOG_NL,
                (unsigned)g_auth_defer.seq);

  /* 成功响应 */
  rcnt = 0U;
  u8_r = PHONE_RESULT_OK;
  rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_AUTH_SESSION_ID; rsp_tlvs[rcnt].len = 16U; rsp_tlvs[rcnt].value = g_sess.auth_session_id; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_CHALLENGE_ID; rsp_tlvs[rcnt].len = 16U; rsp_tlvs[rcnt].value = g_sess.challenge_id; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_NONCE; rsp_tlvs[rcnt].len = 16U; rsp_tlvs[rcnt].value = g_sess.nonce_b; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_BG24_ECDH_PUBLIC_KEY; rsp_tlvs[rcnt].len = 65U; rsp_tlvs[rcnt].value = g_sess.bg_ecdh_pubkey; rcnt++;
  u8_b = g_sess.device_state;
  rsp_tlvs[rcnt].type = PHONE_TLV_BIND_STATE; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_b; rcnt++;

  if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
    (void)send_plain_response(PHONE_CMD_AUTH_CHALLENGE_REQ, g_auth_defer.seq,
                               payload, plen);
  }
}

/* ========================================================================== */
/* 延迟 AUTH_CHALLENGE_RSP: ECDH 共享密钥计算 + AUTH_SIGN_BYTES + SHA-256      */
/* ========================================================================== */

/** 在 phone_sm_process_action 中调用, 完成延迟的 ECDH 计算, 构建签名并提交 ECDSA 验签 */
static void phone_sm_process_ecdh_rsp_deferred(void)
{
  sl_status_t sc;
  uint8_t shared_secret[32];
  uint8_t sign_buf[512];
  uint16_t sign_len;
  uint8_t digest[32];
  const char *prefix = "BLEKEY-AUTH-V1";
  uint16_t u16;
  char did[PHONE_DEVICE_ID_MAX_LEN + 1];
  uint8_t stored_key_id[16];
  uint16_t seq = g_ecdh_rsp_defer.seq;

  if (!g_ecdh_rsp_defer.active) return;
  if (g_ecdsa_defer.active)    return;  /* SE 锁冲突: ECDSA 先完成 */

  /* === Step 1: ECDH 共享密钥 (PSA → SE, 阻塞 ~400ms, CAN 已运行) === */
  sc = phone_crypto_ecdh_compute_shared(g_sess.bg_ecdh_privkey,
                                         g_sess.app_ecdh_pubkey,
                                         shared_secret);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] ECDH_RSP_DEFERRED ECDH_FAIL sc=0x%04lX" USER_LOG_NL,
                  (unsigned long)sc);
    g_ecdh_rsp_defer.active = false;
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq,
                        PHONE_RESULT_FAIL, PHONE_ERR_INTERNAL_ERROR,
                        false, true, false);
    return;
  }

  /* === Step 2: 构建 AUTH_SIGN_BYTES (协议第 10.1 节) === */
  memset(did, 0, sizeof(did));
  phone_storage_get_device_id(did, sizeof(did) - 1U);
  phone_storage_get_app_key_id(stored_key_id);

  sign_len = 0U;
  memcpy(&sign_buf[sign_len], prefix, 14U);  sign_len += 14U;
  sign_buf[sign_len++] = PHONE_FRAME_VERSION;
  u16 = (uint16_t)strlen(did);
  sign_buf[sign_len++] = (u16 >> 8) & 0xFF;  sign_buf[sign_len++] = u16 & 0xFF;
  memcpy(&sign_buf[sign_len], did, u16);     sign_len += u16;
  sign_buf[sign_len++] = 0x00; sign_buf[sign_len++] = 0x10;
  memcpy(&sign_buf[sign_len], stored_key_id, 16U); sign_len += 16U;
  memcpy(&sign_buf[sign_len], g_sess.auth_session_id, 16U); sign_len += 16U;
  memcpy(&sign_buf[sign_len], g_sess.challenge_id, 16U); sign_len += 16U;
  memcpy(&sign_buf[sign_len], g_sess.nonce_a, 16U); sign_len += 16U;
  memcpy(&sign_buf[sign_len], g_sess.nonce_b, 16U); sign_len += 16U;
  memcpy(&sign_buf[sign_len], g_sess.app_ecdh_pubkey, 65U); sign_len += 65U;
  memcpy(&sign_buf[sign_len], g_sess.bg_ecdh_pubkey, 65U); sign_len += 65U;
  {
    uint64_t ctr = g_ecdh_rsp_defer.app_counter;
    uint8_t i;
    for (i = 0U; i < 8U; i++) {
      sign_buf[sign_len++] = (uint8_t)(ctr >> (56U - 8U * i));
    }
  }

  /* === Step 3: SHA-256(AUTH_SIGN_BYTES) === */
  sc = phone_crypto_sha256(sign_buf, sign_len, digest);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[SM] ECDH_RSP_DEFERRED SHA256_FAIL sc=0x%04lX" USER_LOG_NL,
                  (unsigned long)sc);
    g_ecdh_rsp_defer.active = false;
    phone_crypto_memzero(shared_secret, 32U);
    phone_session_record_security_failure(&g_sess);
    send_error_response(PHONE_CMD_AUTH_CHALLENGE_RSP, seq,
                        PHONE_RESULT_FAIL, PHONE_ERR_AUTH_FAILED,
                        false, true, false);
    return;
  }

  /* === Step 4: 移交到 ECDSA 延迟验签队列 === */
  g_ecdh_rsp_defer.active = false;
  g_ecdsa_defer.active       = true;
  g_ecdsa_defer.cmd          = PHONE_CMD_AUTH_CHALLENGE_RSP;
  g_ecdsa_defer.seq          = seq;
  memcpy(g_ecdsa_defer.pub_key, g_ecdh_rsp_defer.pub_key, 65U);
  memcpy(g_ecdsa_defer.digest, digest, 32U);
  memcpy(g_ecdsa_defer.sig, g_ecdh_rsp_defer.sig, 64U);
  g_ecdsa_defer.app_counter = g_ecdh_rsp_defer.app_counter;
  memcpy(g_ecdsa_defer.shared_secret, shared_secret, 32U);
  phone_crypto_memzero(shared_secret, 32U);

  USER_LOG_INFO("[SM] ECDH_RSP_DEFERRED seq=%u → ECDSA deferred" USER_LOG_NL,
                (unsigned)seq);
}

/* ========================================================================== */
/* 延迟 CTRL_CHALLENGE_REQ 响应发送 (send_encrypted_response ~500ms)            */
/* ========================================================================== */

static void phone_sm_process_ctrl_challenge_deferred(void)
{
  uint8_t payload[64];
  uint16_t plen;
  phone_tlv_t rsp_tlvs[4];
  uint8_t rcnt = 0U;
  uint8_t u8_r;
  uint8_t ec_buf[2] = {0, 0};

  if (!g_ctrl_challenge_defer.active) return;

  u8_r = PHONE_RESULT_OK;
  rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U; rsp_tlvs[rcnt].value = &u8_r; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U; rsp_tlvs[rcnt].value = ec_buf; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_CHALLENGE_ID; rsp_tlvs[rcnt].len = 16U; rsp_tlvs[rcnt].value = g_sess.ctrl_challenge_id; rcnt++;
  rsp_tlvs[rcnt].type = PHONE_TLV_NONCE; rsp_tlvs[rcnt].len = 16U; rsp_tlvs[rcnt].value = g_sess.ctrl_nonce; rcnt++;

  if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
    (void)send_encrypted_response(PHONE_CMD_CTRL_CHALLENGE_REQ,
                                   g_ctrl_challenge_defer.seq,
                                   payload, plen, false);
  }
  g_ctrl_challenge_defer.active = false;
}

/* ========================================================================== */
/* Public API                                                                  */
/* ========================================================================== */

void phone_sm_init(phone_sm_send_fn_t send_fn, phone_sm_disconnect_fn_t disc_fn)
{
  g_send_fn  = send_fn;
  g_disc_fn  = disc_fn;
  phone_session_init(&g_sess);
  phone_crypto_init();
  phone_storage_init();
  g_notify_enabled       = false;
  g_connected            = false;
  g_pending_control_cmd  = 0U;
  g_vehicle_lock_state   = PHONE_LOCK_STATE_UNKNOWN;
  g_pending_rx_len       = 0U;
  g_ecdsa_defer.active          = false;
  g_auth_defer.active           = false;
  g_ecdh_rsp_defer.active       = false;
  g_ctrl_challenge_defer.active = false;
}

void phone_sm_process_action(void)
{
  phone_session_process_timeouts(&g_sess);

  /* V1.2: 配对窗口超时检查 (系统 Pairing 60s 未完成 → 恢复 Non-Bondable) */
  if (g_sess.pairing_window_active
      && phone_session_now_ms() > g_sess.pairing_window_deadline_ms) {
    USER_LOG_INFO("[SM] pairing window timeout → destroy" USER_LOG_NL);
    pairing_context_destroy();
  }

  /* === 优先级最高: 完成上一轮延迟的 ECDSA 验签 === */
  if (g_ecdsa_defer.active) {
    phone_sm_deferred_ecdsa_poll();
  }

  /* === 处理延迟的 AUTH_CHALLENGE_REQ (ECDH 密钥生成) === */
  if (g_auth_defer.active && !g_ecdsa_defer.active) {
    phone_sm_process_auth_deferred();
  }

  /* === 处理延迟的 AUTH_CHALLENGE_RSP step1 (ECDH 共享密钥) === */
  if (g_ecdh_rsp_defer.active && !g_ecdsa_defer.active) {
    phone_sm_process_ecdh_rsp_deferred();
  }

  /* === 处理延迟的 CTRL_CHALLENGE_REQ 响应发送 === */
  if (g_ctrl_challenge_defer.active) {
    phone_sm_process_ctrl_challenge_deferred();
  }

  /* === 延迟帧处理: BLE 回调中缓存的数据在主循环中处理 === */
  /* 当 ECDSA 正在延迟处理时(g_ecdsa_defer.active=true), 跳过新帧,
   * 因为 SE 锁被持有, AES-CCM 解密等需要 SE 的操作会阻塞等待锁 */
  if (g_pending_rx_len != 0U && !g_ecdsa_defer.active) {
    uint16_t len = g_pending_rx_len;
    g_pending_rx_len = 0U;  /* 先清零, 允许下一帧进来 */
    process_received_frame(g_pending_rx_data, len);
  }

  /* SILENT 恢复后更新 bindState */
  if (!g_sess.is_silent && g_sess.device_state != phone_storage_get_bind_state()) {
    g_sess.device_state = phone_storage_get_bind_state();
  }

  /* 状态稳定延迟后发送 STATE_CHANGED_EVENT (协议 §13: 100~300ms, V1.2 扩展为5字段) */
  if ((g_sess.pending_lock_state != 0U || g_sess.pending_vehicle_state)
      && phone_session_now_ms() >= g_sess.lock_state_deadline_ms) {
    g_sess.pending_lock_state = 0U;
    g_sess.pending_vehicle_state = false;

    if (phone_session_is_authenticated(&g_sess)) {
      {
        uint8_t payload[64];
        uint16_t plen;
        phone_tlv_t tlvs[6];
        uint8_t tcnt = 0U;
        uint8_t u8_b, u8_l, u8_ign, u8_door;
        uint8_t range_buf[2];

        u8_b = g_sess.device_state;
        tlvs[tcnt].type = PHONE_TLV_BIND_STATE; tlvs[tcnt].len = 1U; tlvs[tcnt].value = &u8_b; tcnt++;

        u8_l = g_vehicle_lock_state;
        tlvs[tcnt].type = PHONE_TLV_VEHICLE_LOCK_STATE; tlvs[tcnt].len = 1U; tlvs[tcnt].value = &u8_l; tcnt++;

        /* V1.2 新增: 3 个车辆状态字段 */
        u8_ign = map_ignition_to_protocol(vehicle_state_get_ignition_gear());
        tlvs[tcnt].type = PHONE_TLV_IGNITION_STATE; tlvs[tcnt].len = 1U; tlvs[tcnt].value = &u8_ign; tcnt++;

        {
          uint16_t raw_range = vehicle_state_get_remaining_range_km();
          phone_frame_write_u16_be(range_buf, raw_range);
        }
        tlvs[tcnt].type = PHONE_TLV_REMAINING_RANGE; tlvs[tcnt].len = 2U; tlvs[tcnt].value = range_buf; tcnt++;

        u8_door = map_doors_to_protocol(vehicle_state_get_door_status());
        tlvs[tcnt].type = PHONE_TLV_DOOR_STATE; tlvs[tcnt].len = 1U; tlvs[tcnt].value = &u8_door; tcnt++;

        USER_LOG_INFO("[SM] STATE_CHANGED_EVENT FIRING lock=%u ign=%u range=%u door=%u (debounced)" USER_LOG_NL,
                      (unsigned)u8_l, (unsigned)u8_ign,
                      (unsigned)phone_frame_read_u16_be(range_buf), (unsigned)u8_door);

        if (phone_tlv_encode(tlvs, tcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
          (void)send_encrypted_event(PHONE_CMD_STATE_CHANGED_EVENT, payload, plen);
        }
      }
    }
  }
}

void phone_sm_on_connection_opened(uint8_t conn_handle)
{
  g_sess.conn_handle = conn_handle;
  g_connected = true;
  g_notify_enabled = false;
  USER_LOG_INFO("[SM] CONNECTED conn=%u state=0x%02X" USER_LOG_NL,
                (unsigned)conn_handle, (unsigned)g_sess.device_state);
}

void phone_sm_on_connection_closed(uint8_t conn_handle)
{
  USER_LOG_INFO("[SM] DISCONNECTED conn=%u" USER_LOG_NL, (unsigned)conn_handle);
  (void)conn_handle;

  /* V1.2 23.2: APP断连 → 立即销毁配对窗口 + 恢复 Non-Bondable;
   *             但等待系统 Pairing 时断连不销毁 (系统复连后继续配对) */
  if (g_sess.pairing_window_active && !g_sess.pairing_awaiting_system) {
    pairing_context_destroy();
  }

  phone_session_reset(&g_sess);
  g_connected    = false;
  g_notify_enabled = false;
  g_pending_control_cmd = 0U;
  g_pending_rx_len      = 0U;
  g_ecdsa_defer.active          = false;
  g_auth_defer.active           = false;
  g_ecdh_rsp_defer.active       = false;
  g_ctrl_challenge_defer.active = false;
}

void phone_sm_on_mtu_exchanged(uint16_t mtu)
{
  g_sess.att_mtu = mtu;
  USER_LOG_INFO("[SM] MTU=%u" USER_LOG_NL, (unsigned)mtu);
}

void phone_sm_on_notify_enabled(void)
{
  g_notify_enabled = true;
  USER_LOG_INFO("[SM] NOTIFY_ENABLED" USER_LOG_NL);
}

void phone_sm_on_receive(const uint8_t *data, uint16_t len)
{
  if (!g_notify_enabled || !g_connected) return;
  if (data == NULL || len == 0U || len > sizeof(g_pending_rx_data)) return;

  /* 保护: 上一帧尚未处理完则丢弃 (正常情况下不应发生) */
  if (g_pending_rx_len != 0U) {
    USER_LOG_INFO("[SM] ⚠️ RX_DROP 上一帧尚未处理(len=%u), 丢弃新帧(len=%u)" USER_LOG_NL,
                  (unsigned)g_pending_rx_len, (unsigned)len);
    return;
  }

  /* 拷贝到延迟处理缓冲区, 由 phone_sm_process_action() 在主循环中处理 */
  memcpy(g_pending_rx_data, data, len);
  g_pending_rx_len = len;
}

bool phone_sm_is_connected(void)
{
  return g_connected;
}

bool phone_sm_is_authenticated(void)
{
  return phone_session_is_authenticated(&g_sess);
}

bool phone_sm_is_notify_enabled(void)
{
  return g_notify_enabled;
}

bool phone_sm_is_passive_enabled(void)
{
  return g_sess.passive_enabled;
}

bool phone_sm_consume_passive_quota(void)
{
  if (g_sess.passive_quota_remaining == 0U) {
    return false;  /* 额度耗尽 */
  }
  g_sess.passive_quota_remaining--;
  (void)phone_storage_set_passive_quota(g_sess.passive_quota_remaining);
  USER_LOG_INFO("[SM] PASSIVE_QUOTA consume -> remaining=%u" USER_LOG_NL,
                (unsigned)g_sess.passive_quota_remaining);
  return true;
}

uint32_t phone_sm_get_passive_quota(void)
{
  return g_sess.passive_quota_remaining;
}

uint8_t phone_sm_get_passive_sensitivity(void)
{
  return g_sess.passive_sensitivity;
}

uint8_t phone_sm_get_pending_control_cmd(void)
{
  uint8_t cmd = g_pending_control_cmd;
  g_pending_control_cmd = 0U;
  return cmd;
}

uint8_t phone_sm_get_vehicle_lock_state(void)
{
  return g_vehicle_lock_state;
}

uint8_t phone_sm_get_device_state(void)
{
  return g_sess.device_state;
}

void phone_sm_notify_vehicle_lock_state(uint8_t lock_state)
{
  /* V1.2: 向后兼容, 内部从 vehicle_state 读取其他 3 字段后调用完整快照 API */
  uint8_t  ignition = map_ignition_to_protocol(vehicle_state_get_ignition_gear());
  uint16_t range    = vehicle_state_get_remaining_range_km();
  uint8_t  doors    = map_doors_to_protocol(vehicle_state_get_door_status());
  phone_sm_notify_vehicle_state(lock_state, ignition, range, doors);
}

/* V1.2 新增: 完整车辆状态快照变化通知 */
void phone_sm_notify_vehicle_state(uint8_t lock, uint8_t ignition,
                                   uint16_t range, uint8_t doors)
{
  bool changed = false;

  if (!phone_session_is_authenticated(&g_sess)) return;
  if (g_sess.is_silent) return;  /* §22.5: SILENT 时不发送 STATE_CHANGED_EVENT */

  if (lock != g_vehicle_lock_state) {
    USER_LOG_INFO("[SM] vehicle_state lock %u→%u" USER_LOG_NL,
                  (unsigned)g_vehicle_lock_state, (unsigned)lock);
    g_vehicle_lock_state = lock;
    changed = true;
  }
  if (ignition != g_vehicle_ignition_state) {
    g_vehicle_ignition_state = ignition;
    changed = true;
  }
  if (range != g_vehicle_remaining_range) {
    g_vehicle_remaining_range = range;
    changed = true;
  }
  if (doors != g_vehicle_door_state) {
    g_vehicle_door_state = doors;
    changed = true;
  }

  if (!changed) return;  /* 全字段无变化, 不推送 */

  USER_LOG_INFO("[SM] STATE_CHANGED_EVENT lock=%u ign=%u range=%u door=%u — 启动稳定延迟200ms" USER_LOG_NL,
                (unsigned)lock, (unsigned)ignition, (unsigned)range, (unsigned)doors);

  /* 协议 §13: 状态稳定后才发送 (完整快照)
   * pending_vehicle_state 触发 debounce, 由 process_action 实际推送 */
  g_sess.pending_vehicle_state = true;
  g_sess.lock_state_deadline_ms = phone_session_now_ms() + PHONE_LOCK_STATE_DEBOUNCE_MS;
}

void phone_sm_set_auth_condition(bool met)
{
  g_auth_condition_met = met;
  USER_LOG_INFO("[SM] auth_condition := %s" USER_LOG_NL, met ? "MET" : "NOT_MET");
}

/* ========================================================================== */
/* V1.2: PASSIVE_PAIR_PREPARE (0x62) — 生成一次性 Pairing 凭据                */
/* ========================================================================== */

static void handle_passive_pair_prepare(uint16_t seq,
                                        const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  (void)tlvs; (void)tlv_count;  /* Payload 为空 */

  USER_LOG_INFO("[SM] PASSIVE_PAIR_PREPARE seq=%u" USER_LOG_NL, (unsigned)seq);

  /* 状态前置检查 */
  if (!phone_session_is_authenticated(&g_sess)) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }
  if (g_sess.is_silent) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, true, true, true);
    return;
  }
  if (g_sess.device_state == PHONE_DEVICE_STATE_SECURITY_LOCKED) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_LOCKED, true, true, false);
    return;
  }
  /* 已有活动配对上下文 → BUSY */
  if (g_sess.pairing_window_active) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_BUSY, true, true, false);
    return;
  }
  /* 本 session 连续失败已达上限 */
  if (g_sess.pairing_fail_count_this_session >= PHONE_PAIRING_MAX_FAIL_PER_SESSION) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }

  /* 生成 8 字节随机 pairingWindowId */
  sl_status_t sc;
  sc = phone_crypto_get_random(g_sess.pairing_window_id, 8U);
  if (sc != SL_STATUS_OK) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_INTERNAL_ERROR, true, true, false);
    return;
  }

  /* 生成 6 位 Passkey (调试固定 PIN 优先, 否则 TRNG 随机) */
  {
    uint32_t fixed_pin = hid_service_get_pin();
    if (fixed_pin >= 100000UL && fixed_pin <= 999999UL) {
      g_sess.pairing_passkey = fixed_pin;
      USER_LOG_INFO("[SM] use fixed debug PIN (value hidden)" USER_LOG_NL);
    } else {
      uint8_t pk_bytes[4];
      sc = phone_crypto_get_random(pk_bytes, 4U);
      if (sc != SL_STATUS_OK) {
        phone_crypto_memzero(g_sess.pairing_window_id, 8U);
        send_error_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, PHONE_RESULT_FAIL,
                            PHONE_ERR_INTERNAL_ERROR, true, true, false);
        return;
      }
      uint32_t raw;
      (void)memcpy(&raw, pk_bytes, 4U);
      phone_crypto_memzero(pk_bytes, 4U);
      g_sess.pairing_passkey = raw % (PHONE_PAIRING_PASSKEY_MAX + 1U);
    }
  }

  /* 启动 30 秒准备期 (Bondable 保持 ON, 安全由 IO 能力控制) */
  g_sess.pairing_window_deadline_ms = phone_session_now_ms()
                                    + (uint64_t)PHONE_TIMEOUT_PAIR_PREPARE_MS;
  g_sess.pairing_window_active = true;

  /* 构造成功响应: result + errorCode + windowId + passkey + remainSec=30 */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[5];
    uint8_t rcnt = 0U;
    uint8_t u8_r;
    uint8_t ec_buf[2] = {0, 0};
    uint8_t remain_buf[2];

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
    rsp_tlvs[rcnt].value = &u8_r; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    /* pairingWindowId (0x26) — 8 字节 */
    rsp_tlvs[rcnt].type = PHONE_TLV_PAIRING_WINDOW_ID; rsp_tlvs[rcnt].len = 8U;
    rsp_tlvs[rcnt].value = g_sess.pairing_window_id; rcnt++;

    /* pairingPasskey (0x27) — U32 BE */
    {
      static uint8_t pk_be[4];
      phone_frame_write_u32_be(pk_be, g_sess.pairing_passkey);
      rsp_tlvs[rcnt].type = PHONE_TLV_PAIRING_PASSKEY; rsp_tlvs[rcnt].len = 4U;
      rsp_tlvs[rcnt].value = pk_be; rcnt++;
    }

    /* remainSec (0x0E) — U16 BE, 值=30 */
    phone_frame_write_u16_be(remain_buf, 30U);
    rsp_tlvs[rcnt].type = PHONE_TLV_REMAIN_SEC; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = remain_buf; rcnt++;

    USER_LOG_INFO("[SM] PASSIVE_PAIR_PREPARE OK (passkey generated)" USER_LOG_NL);

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_PASSIVE_PAIR_PREPARE, seq, payload, plen, false);
    }
  }
}

/* ========================================================================== */
/* V1.2: PASSIVE_PAIR_READY (0x63) — 触发系统 Pairing                         */
/* ========================================================================== */

static void handle_passive_pair_ready(uint16_t seq,
                                       const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  USER_LOG_INFO("[SM] PASSIVE_PAIR_READY seq=%u" USER_LOG_NL, (unsigned)seq);

  /* 状态检查 */
  if (!phone_session_is_authenticated(&g_sess)) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_READY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }
  if (g_sess.is_silent) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_READY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, true, true, true);
    return;
  }

  /* TLV 校验: pairingWindowId 必选, 长度必须为 8 字节 */
  {
    const phone_tlv_t *t = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_PAIRING_WINDOW_ID);
    if (t == NULL || t->len != 8U) {
      send_error_response(PHONE_CMD_PASSIVE_PAIR_READY, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PARAM_INVALID, true, true, false);
      return;
    }
  }

  /* 窗口校验 */
  if (!g_sess.pairing_window_active) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_READY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PAIRING_WINDOW_INVALID, true, true, false);
    return;
  }
  if (phone_session_now_ms() > g_sess.pairing_window_deadline_ms) {
    pairing_context_destroy();
    send_error_response(PHONE_CMD_PASSIVE_PAIR_READY, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PAIRING_WINDOW_INVALID, true, true, false);
    return;
  }
  {
    const phone_tlv_t *t = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_PAIRING_WINDOW_ID);
    if (memcmp(t->value, g_sess.pairing_window_id, 8U) != 0) {
      send_error_response(PHONE_CMD_PASSIVE_PAIR_READY, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PAIRING_WINDOW_INVALID, true, true, false);
      return;
    }
  }

  /* 先发送成功响应 (协议要求: APP 先收到确认, 再开启 BLE Pairing) */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[4];
    uint8_t rcnt = 0U;
    uint8_t u8_r;
    uint8_t ec_buf[2] = {0, 0};
    uint8_t remain_buf[2];

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
    rsp_tlvs[rcnt].value = &u8_r; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    /* pairingWindowId 回显 */
    rsp_tlvs[rcnt].type = PHONE_TLV_PAIRING_WINDOW_ID; rsp_tlvs[rcnt].len = 8U;
    rsp_tlvs[rcnt].value = g_sess.pairing_window_id; rcnt++;

    /* remainSec = 60 */
    phone_frame_write_u16_be(remain_buf, 60U);
    rsp_tlvs[rcnt].type = PHONE_TLV_REMAIN_SEC; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = remain_buf; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_PASSIVE_PAIR_READY, seq, payload, plen, false);
    }
  }

  /* 响应发送完成后: 开启 BLE Pairing 窗口 */
  {
    sl_status_t sc;
    uint64_t ts;

    USER_LOG_INFO("[SM] === PAIR_READY SM config start ===" USER_LOG_NL);

    ts = phone_session_now_ms();
    sc = sl_bt_sm_store_bonding_configuration(2, 0);
    USER_LOG_INFO("[SM]   [%lums] sm_store_bonds(2) sc=0x%04lx" USER_LOG_NL,
                  (unsigned long)ts, (unsigned long)sc);

    ts = phone_session_now_ms();
    sc = sl_bt_sm_configure(SL_BT_SM_CONFIGURATION_SC_ONLY
                            | SL_BT_SM_CONFIGURATION_MITM_REQUIRED
                            | SL_BT_SM_CONFIGURATION_BONDING_REQUIRED,
                             sl_bt_sm_io_capability_displayonly);
    USER_LOG_INFO("[SM]   [%lums] sm_configure(SC+MITM+BR+DisplayOnly) sc=0x%04lx" USER_LOG_NL,
                  (unsigned long)ts, (unsigned long)sc);

    ts = phone_session_now_ms();
    sc = sl_bt_sm_set_passkey((int32_t)g_sess.pairing_passkey);
    USER_LOG_INFO("[SM]   [%lums] sm_set_passkey(******) sc=0x%04lx" USER_LOG_NL,
                  (unsigned long)ts, (unsigned long)sc);

    ts = phone_session_now_ms();
    sc = sl_bt_sm_set_bondable_mode(1);
    USER_LOG_INFO("[SM]   [%lums] sm_set_bondable(1) sc=0x%04lx" USER_LOG_NL,
                  (unsigned long)ts, (unsigned long)sc);

    /* V1.2 23.6 系统配对模式: 广播 HID UUID + 断开 APP, 让系统 HID Host 扫描到 HID 设备并配对
     * (设备连着 APP 时广播已停, 系统看不到 HID; 断开后恢复广播带 HID UUID) */
    g_sess.pairing_awaiting_system = true;
    hid_service_set_runtime_enabled(true);
    ts = phone_session_now_ms();
    sc = sl_bt_connection_close(g_sess.conn_handle);
    USER_LOG_INFO("[SM]   [%lums] system pairing: HID adv ON + close APP conn=%u sc=0x%04lx" USER_LOG_NL,
                  (unsigned long)ts, (unsigned)g_sess.conn_handle, (unsigned long)sc);
  }

  g_sess.pairing_window_deadline_ms = phone_session_now_ms()
                                    + (uint64_t)PHONE_TIMEOUT_PAIR_READY_MS;
}

/* ========================================================================== */
/* V1.2: PASSIVE_PAIR_CANCEL (0x64) — 取消配对上下文                          */
/* ========================================================================== */

static void handle_passive_pair_cancel(uint16_t seq,
                                        const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  USER_LOG_INFO("[SM] PASSIVE_PAIR_CANCEL seq=%u" USER_LOG_NL, (unsigned)seq);

  /* 状态检查 */
  if (!phone_session_is_authenticated(&g_sess)) {
    send_error_response(PHONE_CMD_PASSIVE_PAIR_CANCEL, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }

  /* 可选 TLV 校验: 有活动上下文 + 带 ID 时需匹配 (不匹配也执行清理, 只是记录) */
  if (g_sess.pairing_window_active) {
    const phone_tlv_t *t = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_PAIRING_WINDOW_ID);
    if (t != NULL) {
      if (t->len != 8U || memcmp(t->value, g_sess.pairing_window_id, 8U) != 0) {
        USER_LOG_INFO("[SM] PASSIVE_PAIR_CANCEL windowId mismatch, still cleaning" USER_LOG_NL);
      }
    }
  }

  /* 幂等清理 */
  pairing_context_destroy();

  /* 始终返回 SUCCESS (协议: 无活动上下文时作为幂等清理同样返回 SUCCESS) */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[2];
    uint8_t rcnt = 0U;
    uint8_t u8_r;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
    rsp_tlvs[rcnt].value = &u8_r; rcnt++;

    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    USER_LOG_INFO("[SM] PASSIVE_PAIR_CANCEL OK" USER_LOG_NL);

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_PASSIVE_PAIR_CANCEL, seq, payload, plen, false);
    }
  }
}

/* ========================================================================== */
/* V1.2: SM 事件回调 — Pairing 成功                                           */
/* ========================================================================== */

void phone_sm_on_sm_bonded(uint8_t connection)
{
  (void)connection;
  USER_LOG_INFO("[SM] SM_BONDED conn=%u" USER_LOG_NL, (unsigned)connection);

  if (g_sess.pairing_window_active) {
    pairing_context_destroy();
    USER_LOG_INFO("[SM] Pairing succeeded, bond written, passiveEnabled still OFF" USER_LOG_NL);
  }
}

/* ========================================================================== */
/* V1.2: SM 事件回调 — Pairing 失败                                           */
/* ========================================================================== */

void phone_sm_on_sm_bonding_failed(uint8_t connection, uint16_t reason)
{
  (void)connection;
  USER_LOG_INFO("[SM] SM_BONDING_FAILED conn=%u reason=0x%04X" USER_LOG_NL,
                (unsigned)connection, (unsigned)reason);

  if (g_sess.pairing_window_active) {
    pairing_context_destroy();
    g_sess.pairing_fail_count_this_session++;
    USER_LOG_INFO("[SM] Pairing failed #%u this session" USER_LOG_NL,
                  (unsigned)g_sess.pairing_fail_count_this_session);
  }
}

/* ========================================================================== */
/* V1.2: PASSIVE_ENABLE (0x60) — 启用无感钥匙                                  */
/* ========================================================================== */

static void handle_passive_enable(uint16_t seq,
                                   const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  (void)tlvs; (void)tlv_count;

  USER_LOG_INFO("[SM] PASSIVE_ENABLE seq=%u" USER_LOG_NL, (unsigned)seq);

  /* 状态检查 */
  if (!phone_session_is_authenticated(&g_sess)) {
    send_error_response(PHONE_CMD_PASSIVE_ENABLE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }
  if (g_sess.is_silent) {
    send_error_response(PHONE_CMD_PASSIVE_ENABLE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, true, true, true);
    return;
  }
  if (g_sess.device_state == PHONE_DEVICE_STATE_SECURITY_LOCKED) {
    send_error_response(PHONE_CMD_PASSIVE_ENABLE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_LOCKED, true, true, false);
    return;
  }

  /* 幂等: 已开启 → 直接返回 SUCCESS */
  if (g_sess.passive_enabled) {
    USER_LOG_INFO("[SM] PASSIVE_ENABLE already enabled (idempotent)" USER_LOG_NL);
    {
      uint8_t payload[64];
      uint16_t plen;
      phone_tlv_t rsp_tlvs[2];
      uint8_t rcnt = 0U;
      uint8_t u8_r;
      uint8_t ec_buf[2] = {0, 0};

      u8_r = PHONE_RESULT_OK;
      rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
      rsp_tlvs[rcnt].value = &u8_r; rcnt++;
      rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
      rsp_tlvs[rcnt].value = ec_buf; rcnt++;

      if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
        (void)send_encrypted_response(PHONE_CMD_PASSIVE_ENABLE, seq, payload, plen, false);
      }
    }
    return;
  }

  /* Bond 存在性检查: 查询 BLE bonding 表 */
  /* 协议 §23.2: 无有效 Bond → 返回 PAIRING_REQUIRED(0x000C), 需走 Pairing */
  if (!hid_service_is_bonded()) {
    USER_LOG_INFO("[SM] PASSIVE_ENABLE: no bond → PAIRING_REQUIRED" USER_LOG_NL);
    send_error_response(PHONE_CMD_PASSIVE_ENABLE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_PAIRING_REQUIRED, true, true, false);
    return;
  }

  /* Bond 存在: 启用无感 */
  g_sess.passive_enabled = true;
  g_sess.hid_runtime_enabled = true;
  hid_service_set_runtime_enabled(true);  /* 广播加入 HID 特征, 供 OS 后台回连 */
  (void)phone_storage_set_passive_enabled(true);

  USER_LOG_INFO("[SM] PASSIVE_ENABLE OK → passiveEnabled=ON, hidRuntime=ON" USER_LOG_NL);

  /* 成功响应 */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[2];
    uint8_t rcnt = 0U;
    uint8_t u8_r;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
    rsp_tlvs[rcnt].value = &u8_r; rcnt++;
    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_PASSIVE_ENABLE, seq, payload, plen, false);
    }
  }
}

/* ========================================================================== */
/* V1.2: PASSIVE_DISABLE (0x61) — 关闭无感钥匙                                 */
/* ========================================================================== */

static void handle_passive_disable(uint16_t seq,
                                    const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  (void)tlvs; (void)tlv_count;

  USER_LOG_INFO("[SM] PASSIVE_DISABLE seq=%u" USER_LOG_NL, (unsigned)seq);

  /* 状态检查 */
  if (!phone_session_is_authenticated(&g_sess)) {
    send_error_response(PHONE_CMD_PASSIVE_DISABLE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }
  if (g_sess.is_silent) {
    send_error_response(PHONE_CMD_PASSIVE_DISABLE, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, true, true, true);
    return;
  }

  /* 幂等: 已关闭 → 直接返回 SUCCESS */
  if (!g_sess.passive_enabled) {
    USER_LOG_INFO("[SM] PASSIVE_DISABLE already disabled (idempotent)" USER_LOG_NL);
    {
      uint8_t payload[64];
      uint16_t plen;
      phone_tlv_t rsp_tlvs[2];
      uint8_t rcnt = 0U;
      uint8_t u8_r;
      uint8_t ec_buf[2] = {0, 0};

      u8_r = PHONE_RESULT_OK;
      rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
      rsp_tlvs[rcnt].value = &u8_r; rcnt++;
      rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
      rsp_tlvs[rcnt].value = ec_buf; rcnt++;

      if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
        (void)send_encrypted_response(PHONE_CMD_PASSIVE_DISABLE, seq, payload, plen, false);
      }
    }
    return;
  }

  /* 如有活动 Pairing 上下文 → 一并取消 */
  if (g_sess.pairing_window_active) {
    pairing_context_destroy();
    USER_LOG_INFO("[SM] PASSIVE_DISABLE: also cleaned pairing context" USER_LOG_NL);
  }

  /* 关闭无感 */
  g_sess.passive_enabled = false;
  g_sess.hid_runtime_enabled = false;
  hid_service_set_runtime_enabled(false);  /* 广播移除 HID 特征 */
  (void)phone_storage_set_passive_enabled(false);

  USER_LOG_INFO("[SM] PASSIVE_DISABLE OK → passiveEnabled=OFF, hidRuntime=OFF" USER_LOG_NL);

  /* 成功响应 */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[2];
    uint8_t rcnt = 0U;
    uint8_t u8_r;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
    rsp_tlvs[rcnt].value = &u8_r; rcnt++;
    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_PASSIVE_DISABLE, seq, payload, plen, false);
    }
  }
}

/* ========================================================================== */
/* V1.2: PASSIVE_SENSITIVITY_SET (0x65) — 设置无感灵敏度                      */
/* ========================================================================== */

static void handle_passive_sensitivity_set(uint16_t seq,
                                            const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  USER_LOG_INFO("[SM] PASSIVE_SENSITIVITY_SET seq=%u" USER_LOG_NL, (unsigned)seq);

  /* 状态检查 */
  if (!phone_session_is_authenticated(&g_sess)) {
    send_error_response(PHONE_CMD_PASSIVE_SENSITIVITY_SET, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }
  if (g_sess.is_silent) {
    send_error_response(PHONE_CMD_PASSIVE_SENSITIVITY_SET, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, true, true, true);
    return;
  }

  /* TLV 解析: passiveSensitivity (0x28), U8, 必选 */
  {
    const phone_tlv_t *t = phone_tlv_get(tlvs, tlv_count, PHONE_TLV_PASSIVE_SENSITIVITY);
    if (t == NULL || t->len != 1U) {
      send_error_response(PHONE_CMD_PASSIVE_SENSITIVITY_SET, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PARAM_INVALID, true, true, false);
      return;
    }
    uint8_t sens = t->value[0];
    if (sens < PHONE_PASSIVE_SENS_NEAR || sens > PHONE_PASSIVE_SENS_FAR) {
      send_error_response(PHONE_CMD_PASSIVE_SENSITIVITY_SET, seq, PHONE_RESULT_FAIL,
                          PHONE_ERR_PARAM_INVALID, true, true, false);
      return;
    }

    g_sess.passive_sensitivity = sens;
    (void)phone_storage_set_passive_sensitivity(sens);

    USER_LOG_INFO("[SM] PASSIVE_SENSITIVITY_SET OK sens=%u" USER_LOG_NL, (unsigned)sens);
  }

  /* 成功响应 */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[2];
    uint8_t rcnt = 0U;
    uint8_t u8_r;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
    rsp_tlvs[rcnt].value = &u8_r; rcnt++;
    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_PASSIVE_SENSITIVITY_SET, seq, payload, plen, false);
    }
  }
}

/* ========================================================================== */
/* V1.2: PASSIVE_QUOTA_REFRESH (0x66) — 重置自动解锁额度                      */
/* ========================================================================== */

static void handle_passive_quota_refresh(uint16_t seq,
                                          const phone_tlv_t *tlvs, uint8_t tlv_count)
{
  (void)tlvs; (void)tlv_count;

  USER_LOG_INFO("[SM] PASSIVE_QUOTA_REFRESH seq=%u" USER_LOG_NL, (unsigned)seq);

  /* 状态检查 */
  if (!phone_session_is_authenticated(&g_sess)) {
    send_error_response(PHONE_CMD_PASSIVE_QUOTA_REFRESH, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_STATE_NOT_ALLOWED, true, true, false);
    return;
  }
  if (g_sess.is_silent) {
    send_error_response(PHONE_CMD_PASSIVE_QUOTA_REFRESH, seq, PHONE_RESULT_FAIL,
                        PHONE_ERR_DEVICE_SILENT, true, true, true);
    return;
  }

  /* 重置额度为 N (协议: 不是累加, 是重置) + 持久化到 NVM */
  g_sess.passive_quota_remaining = PHONE_PASSIVE_QUOTA_DEFAULT;
  (void)phone_storage_set_passive_quota(g_sess.passive_quota_remaining);

  USER_LOG_INFO("[SM] PASSIVE_QUOTA_REFRESH OK — quota set to %u" USER_LOG_NL,
                (unsigned)g_sess.passive_quota_remaining);

  /* 成功响应 (不返回 N 或剩余次数, 协议 §23.9) */
  {
    uint8_t payload[64];
    uint16_t plen;
    phone_tlv_t rsp_tlvs[2];
    uint8_t rcnt = 0U;
    uint8_t u8_r;
    uint8_t ec_buf[2] = {0, 0};

    u8_r = PHONE_RESULT_OK;
    rsp_tlvs[rcnt].type = PHONE_TLV_RESULT; rsp_tlvs[rcnt].len = 1U;
    rsp_tlvs[rcnt].value = &u8_r; rcnt++;
    rsp_tlvs[rcnt].type = PHONE_TLV_ERROR_CODE; rsp_tlvs[rcnt].len = 2U;
    rsp_tlvs[rcnt].value = ec_buf; rcnt++;

    if (phone_tlv_encode(rsp_tlvs, rcnt, payload, &plen, sizeof(payload)) == SL_STATUS_OK) {
      (void)send_encrypted_response(PHONE_CMD_PASSIVE_QUOTA_REFRESH, seq, payload, plen, false);
    }
  }
}
