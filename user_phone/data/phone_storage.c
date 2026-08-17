/***************************************************************************//**
 * @file phone_storage.c
 * @brief V1.1 手机协议持久化存储 — 基于 user_eeprom 统一存储层。
 *
 * 所有 NVM 操作委托给 user_eeprom, 本模块仅提供类型化 getter/setter 封装。
 *
 * 原子换绑策略 (本模块管理):
 *   1. user_eeprom_write(BAK_PUBKEY)   → 写入备用区
 *   2. user_eeprom_read(BAK_PUBKEY)    → 回读校验
 *   3. user_eeprom_write(APP_PUBKEY)   → 写入主区
 *   4. user_eeprom_delete(BAK_PUBKEY)  → 清除备用区
 ******************************************************************************/

#include "user_phone/data/phone_storage.h"
#include "user_phone/data/phone_frame.h"
#include "user_phone/phone_cfg.h"
#include "user_eeprom/user_eeprom.h"
#include "user_vin.h"
#include "user_log_console.h"
#include "em_system.h"
#include <string.h>
#include <stdio.h>

/* ARM 32-bit newlib-nano printf 有 %ll 格式 bug, 用纯手动格式化避开 */
static const char *u64_hex_str(uint64_t val)
{
  static char buf[2][19];
  static int idx = 0;
  char *b = buf[(idx++) & 1];
  static const char hex[] = "0123456789ABCDEF";
  int i;
  b[0] = '0'; b[1] = 'x';
  for (i = 0; i < 16; i++) { b[2 + i] = hex[(val >> (60U - 4U * i)) & 0xFU]; }
  b[18] = '\0';
  return b;
}

static const char *u64_dec_str(uint64_t val)
{
  static char buf[2][21];
  static int idx = 0;
  char *b = buf[(idx++) & 1];
  int pos = 20;
  b[pos--] = '\0';
  if (val == 0ULL) { b[pos--] = '0'; }
  else { while (val > 0ULL) { b[pos--] = (char)('0' + (unsigned)(val % 10ULL)); val /= 10ULL; } }
  return &b[pos + 1];
}

/* ========================================================================== */
/* 内部: deviceId RAM 缓存 (上电时从芯片 UID 派生, 不存 EEPROM)               */
/* ========================================================================== */

static char g_device_id[PHONE_DEVICE_ID_MAX_LEN + 1];

static void generate_device_id(void)
{
  /* BG24 64-bit Unique Serial Number → 8 字节, 相邻字节相加得 4 字节 */
  uint64_t uid = SYSTEM_GetUnique();
  uint8_t uid_bytes[8];
  memcpy(uid_bytes, &uid, sizeof(uid_bytes));  /* LE: uid_bytes[0] = LSB */

  uint8_t derived[4];
  derived[0] = uid_bytes[0] + uid_bytes[1];
  derived[1] = uid_bytes[2] + uid_bytes[3];
  derived[2] = uid_bytes[4] + uid_bytes[5];
  derived[3] = uid_bytes[6] + uid_bytes[7];

  snprintf(g_device_id, sizeof(g_device_id), "BG24_%02X%02X%02X%02X",
           (unsigned)derived[0], (unsigned)derived[1],
           (unsigned)derived[2], (unsigned)derived[3]);
}

/* ========================================================================== */
/* 内部: 将不定长字符串写入固定长度条目 (零填充)                              */
/* ========================================================================== */

static sl_status_t write_fixed_str(user_eeprom_item_t item, const char *str,
                                   uint16_t str_len, uint16_t field_size)
{
  uint8_t buf[64];
  if (str == NULL) return SL_STATUS_INVALID_PARAMETER;
  if (str_len == 0U || str_len > field_size || field_size > sizeof(buf))
    return SL_STATUS_INVALID_PARAMETER;

  memset(buf, 0, field_size);
  memcpy(buf, str, str_len);
  return user_eeprom_write(item, buf, field_size, NULL);
}

/* ========================================================================== */
/* 初始化 / 工厂复位                                                          */
/* ========================================================================== */

sl_status_t phone_storage_init(void)
{
  /* 上电时从芯片 64-bit Unique ID 派生 deviceId, 仅存 RAM */
  generate_device_id();

  /* user_eeprom 已由 app_init 统一初始化 */

  /* 上电后打印所有存储信息, 便于排查 */
  phone_storage_dump_all();

  return SL_STATUS_OK;
}

void phone_storage_dump_all(void)
{
  char   did[PHONE_DEVICE_ID_MAX_LEN + 1];
  char   qid[PHONE_TLV_MAX_QID + 1];
  uint32_t cv;
  uint8_t bind_secret[16];
  uint8_t factory_pubkey[65];
  uint8_t app_pubkey[65];
  uint8_t app_key_id[16];
  uint32_t bind_ver;
  uint8_t  bind_state;
  uint64_t app_counter;

  sl_status_t did_sc  = phone_storage_get_device_id(did, sizeof(did));
  sl_status_t qid_sc  = phone_storage_get_qid(qid, sizeof(qid));
  cv                   = phone_storage_get_cv();
  sl_status_t sec_sc   = phone_storage_get_bind_secret(bind_secret);
  sl_status_t fac_sc   = phone_storage_get_factory_pubkey(factory_pubkey);
  sl_status_t appk_sc  = phone_storage_get_app_public_key(app_pubkey);
  sl_status_t akid_sc  = phone_storage_get_app_key_id(app_key_id);
  bind_ver             = phone_storage_get_bind_version();
  bind_state           = phone_storage_get_bind_state();
  app_counter          = phone_storage_get_app_counter();

  USER_LOG_INFO("============================================" USER_LOG_NL);
  USER_LOG_INFO("📋 [STORAGE] 上电存储信息总览" USER_LOG_NL);
  USER_LOG_INFO("────────────────────────────────────────────" USER_LOG_NL);

  /* ---- 产线数据 ---- */
  USER_LOG_INFO("🆔 deviceId:    %s (sc=0x%04lX) [RAM, 芯片UID派生]" USER_LOG_NL,
                (did_sc == SL_STATUS_OK && did[0] != '\0') ? did : "(empty)", (unsigned long)did_sc);
  USER_LOG_INFO("📋 qid:         %s (sc=0x%04lX) [EEPROM]" USER_LOG_NL,
                (qid_sc == SL_STATUS_OK && qid[0] != '\0') ? qid : "(empty)", (unsigned long)qid_sc);
  {
    bool cv_valid = user_eeprom_is_valid(EEPROM_PHONE_CV);
    USER_LOG_INFO("🔢 cv:          0x%08lX / %lu (valid=%s) [EEPROM]" USER_LOG_NL,
                  (unsigned long)cv, (unsigned long)cv, cv_valid ? "yes" : "no");
  }
  USER_LOG_INFO("🔐 bindSecret:  %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X [EEPROM, sc=0x%04lX]" USER_LOG_NL,
                (unsigned)bind_secret[0], (unsigned)bind_secret[1],
                (unsigned)bind_secret[2], (unsigned)bind_secret[3],
                (unsigned)bind_secret[4], (unsigned)bind_secret[5],
                (unsigned)bind_secret[6], (unsigned)bind_secret[7],
                (unsigned)bind_secret[8], (unsigned)bind_secret[9],
                (unsigned)bind_secret[10], (unsigned)bind_secret[11],
                (unsigned)bind_secret[12], (unsigned)bind_secret[13],
                (unsigned)bind_secret[14], (unsigned)bind_secret[15],
                (unsigned long)sec_sc);

  /* ---- 厂家公钥 ---- */
  {
    bool fac_valid = user_eeprom_is_valid(EEPROM_PHONE_FACTORY_PUBKEY);
    if (fac_sc == SL_STATUS_OK) {
      USER_LOG_INFO("🏭 factoryPubkey: [0]=0x%02X %02X %02X %02X %02X %02X %02X %02X ... %02X %02X (65B) [EEPROM, valid=%s]" USER_LOG_NL,
                    (unsigned)factory_pubkey[0], (unsigned)factory_pubkey[1],
                    (unsigned)factory_pubkey[2], (unsigned)factory_pubkey[3],
                    (unsigned)factory_pubkey[4], (unsigned)factory_pubkey[5],
                    (unsigned)factory_pubkey[6], (unsigned)factory_pubkey[7],
                    (unsigned)factory_pubkey[63], (unsigned)factory_pubkey[64],
                    fac_valid ? "yes" : "no");
    } else {
      USER_LOG_INFO("🏭 factoryPubkey: (empty/failed, sc=0x%04lX) [EEPROM, valid=%s]" USER_LOG_NL,
                    (unsigned long)fac_sc, fac_valid ? "yes" : "no");
    }
  }

  USER_LOG_INFO("────────────────────────────────────────────" USER_LOG_NL);

  /* ---- 绑定记录 ---- */
  if (appk_sc == SL_STATUS_OK) {
    USER_LOG_INFO("🔑 appPublicKey: [0]=0x%02X %02X %02X %02X %02X %02X %02X %02X ... %02X %02X (65B) [EEPROM]" USER_LOG_NL,
                  (unsigned)app_pubkey[0], (unsigned)app_pubkey[1],
                  (unsigned)app_pubkey[2], (unsigned)app_pubkey[3],
                  (unsigned)app_pubkey[4], (unsigned)app_pubkey[5],
                  (unsigned)app_pubkey[6], (unsigned)app_pubkey[7],
                  (unsigned)app_pubkey[63], (unsigned)app_pubkey[64]);
  } else {
    USER_LOG_INFO("🔑 appPublicKey: (empty/failed, sc=0x%04lX) [EEPROM]" USER_LOG_NL, (unsigned long)appk_sc);
  }
  if (akid_sc == SL_STATUS_OK) {
    USER_LOG_INFO("🏷️  appKeyId:     %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %02X [EEPROM]" USER_LOG_NL,
                  (unsigned)app_key_id[0], (unsigned)app_key_id[1],
                  (unsigned)app_key_id[2], (unsigned)app_key_id[3],
                  (unsigned)app_key_id[4], (unsigned)app_key_id[5],
                  (unsigned)app_key_id[6], (unsigned)app_key_id[7],
                  (unsigned)app_key_id[8], (unsigned)app_key_id[9],
                  (unsigned)app_key_id[10], (unsigned)app_key_id[11],
                  (unsigned)app_key_id[12], (unsigned)app_key_id[13],
                  (unsigned)app_key_id[14], (unsigned)app_key_id[15]);
  } else {
    USER_LOG_INFO("🏷️  appKeyId:     (empty/failed, sc=0x%04lX) [EEPROM]" USER_LOG_NL, (unsigned long)akid_sc);
  }
  USER_LOG_INFO("#️⃣  bindVersion:  %lu (0x%08lX) [EEPROM]" USER_LOG_NL,
                (unsigned long)bind_ver, (unsigned long)bind_ver);
  USER_LOG_INFO("🔗 bindState:    0x%02X (%s) [EEPROM]" USER_LOG_NL,
                (unsigned)bind_state,
                (bind_state == PHONE_DEVICE_STATE_UNBOUND) ? "UNBOUND" :
                (bind_state == PHONE_DEVICE_STATE_BOUND)   ? "BOUND" :
                (bind_state == PHONE_DEVICE_STATE_REBIND_WINDOW) ? "REBIND_WINDOW" :
                (bind_state == PHONE_DEVICE_STATE_SILENT)  ? "SILENT" :
                (bind_state == PHONE_DEVICE_STATE_SECURITY_LOCKED) ? "SECURITY_LOCKED" :
                (bind_state == PHONE_DEVICE_STATE_SERVICE_MODE) ? "SERVICE_MODE" : "?");
  USER_LOG_INFO("🔢 appCounter:   %s / %s [EEPROM]" USER_LOG_NL,
                u64_hex_str(app_counter), u64_dec_str(app_counter));

  USER_LOG_INFO("============================================" USER_LOG_NL);
}

sl_status_t phone_storage_factory_reset(void)
{
  user_eeprom_factory_reset();
  /* 重置 bindState 为 UNBOUND */
  uint8_t st = PHONE_DEVICE_STATE_UNBOUND;
  return user_eeprom_write(EEPROM_PHONE_BIND_STATE, &st, 1U, NULL);
}

/* ========================================================================== */
/* 产线数据: deviceId (上电时从芯片 UID 派生, RAM 缓存, 不存 EEPROM)         */
/* ========================================================================== */

sl_status_t phone_storage_get_device_id(char *buf, uint8_t max_len)
{
  if (buf == NULL || max_len == 0U) return SL_STATUS_INVALID_PARAMETER;

  uint16_t len = (uint16_t)strlen(g_device_id);
  if (len + 1U > max_len) return SL_STATUS_WOULD_OVERFLOW;

  memcpy(buf, g_device_id, len + 1U);  /* 含 '\0' */
  return SL_STATUS_OK;
}

/* ========================================================================== */
/* 产线数据: qid                                                              */
/* ========================================================================== */

sl_status_t phone_storage_get_qid(char *buf, uint8_t max_len)
{
  uint8_t raw[32];
  if (buf == NULL || max_len == 0U) return SL_STATUS_INVALID_PARAMETER;
  memset(buf, 0, max_len);

  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_QID, raw, sizeof(raw));
  if (sc != SL_STATUS_OK) return sc;

  uint8_t copy = (max_len - 1U < sizeof(raw)) ? (max_len - 1U) : (uint8_t)sizeof(raw);
  memcpy(buf, raw, copy);
  return SL_STATUS_OK;
}

sl_status_t phone_storage_set_qid(const char *qid)
{
  if (qid == NULL) return SL_STATUS_INVALID_PARAMETER;
  uint16_t len = (uint16_t)strlen(qid);
  if (len == 0U || len > PHONE_TLV_MAX_QID) return SL_STATUS_INVALID_PARAMETER;
  return write_fixed_str(EEPROM_PHONE_QID, qid, len, 32U);
}

/* ========================================================================== */
/* 产线数据: cv (U32 BE)                                                      */
/* ========================================================================== */

uint32_t phone_storage_get_cv(void)
{
  uint8_t buf[4];
  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_CV, buf, sizeof(buf));
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ⚠️ CV read failed sc=0x%04lX — 返回默认值 1" USER_LOG_NL,
                  (unsigned long)sc);
    return 1U;
  }
  return phone_frame_read_u32_be(buf);
}

sl_status_t phone_storage_set_cv(uint32_t cv)
{
  uint8_t buf[4];
  phone_frame_write_u32_be(buf, cv);
  return user_eeprom_write(EEPROM_PHONE_CV, buf, 4U, NULL);
}

/* ========================================================================== */
/* 产线数据: bindSecret (16 Byte)                                             */
/* ========================================================================== */

sl_status_t phone_storage_get_bind_secret(uint8_t secret[16])
{
  if (secret == NULL) return SL_STATUS_INVALID_PARAMETER;
  return user_eeprom_read(EEPROM_PHONE_BIND_SECRET, secret, 16U);
}

sl_status_t phone_storage_set_bind_secret(const uint8_t secret[16])
{
  if (secret == NULL) return SL_STATUS_INVALID_PARAMETER;
  return user_eeprom_write(EEPROM_PHONE_BIND_SECRET, secret, 16U, NULL);
}

/* ========================================================================== */
/* 厂家公钥 (65 Byte)                                                          */
/* ========================================================================== */

sl_status_t phone_storage_get_factory_pubkey(uint8_t pubkey[65])
{
  if (pubkey == NULL) return SL_STATUS_INVALID_PARAMETER;
  return user_eeprom_read(EEPROM_PHONE_FACTORY_PUBKEY, pubkey, 65U);
}

sl_status_t phone_storage_set_factory_pubkey(const uint8_t pubkey[65])
{
  if (pubkey == NULL) return SL_STATUS_INVALID_PARAMETER;
  return user_eeprom_write(EEPROM_PHONE_FACTORY_PUBKEY, pubkey, 65U, NULL);
}

/* ========================================================================== */
/* 绑定记录: APP 公钥 (65 Byte)                                               */
/* ========================================================================== */

sl_status_t phone_storage_get_app_public_key(uint8_t key[65])
{
  if (key == NULL) return SL_STATUS_INVALID_PARAMETER;
  return user_eeprom_read(EEPROM_PHONE_APP_PUBKEY, key, 65U);
}

/* ========================================================================== */
/* 绑定记录: appKeyId (16 Byte)                                               */
/* ========================================================================== */

sl_status_t phone_storage_get_app_key_id(uint8_t id[16])
{
  if (id == NULL) return SL_STATUS_INVALID_PARAMETER;
  return user_eeprom_read(EEPROM_PHONE_APP_KEY_ID, id, 16U);
}

/* ========================================================================== */
/* 绑定记录: bindVersion (U32 BE)                                              */
/* ========================================================================== */

uint32_t phone_storage_get_bind_version(void)
{
  uint8_t buf[4];
  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_BIND_VERSION, buf, 4U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ⚠️ bindVersion read failed sc=0x%04lX — 返回默认值 0" USER_LOG_NL,
                  (unsigned long)sc);
    return 0U;
  }
  return phone_frame_read_u32_be(buf);
}

/* ========================================================================== */
/* 绑定状态 (U8)                                                               */
/* ========================================================================== */

uint8_t phone_storage_get_bind_state(void)
{
  uint8_t st = PHONE_DEVICE_STATE_UNBOUND;
  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_BIND_STATE, &st, 1U);
  if (sc != SL_STATUS_OK) return PHONE_DEVICE_STATE_UNBOUND;
  return st;
}

sl_status_t phone_storage_set_bind_state(uint8_t state)
{
  return user_eeprom_write(EEPROM_PHONE_BIND_STATE, &state, 1U, NULL);
}

/* ========================================================================== */
/* V1.2 PASSIVE 状态持久化                                                     */
/* ========================================================================== */

sl_status_t phone_storage_get_passive_enabled(bool *enabled)
{
  if (enabled == NULL) return SL_STATUS_INVALID_PARAMETER;
  uint8_t val = 0U;
  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_PASSIVE_ENABLED, &val, 1U);
  if (sc != SL_STATUS_OK) { *enabled = false; return sc; }
  *enabled = (val != 0U);
  return SL_STATUS_OK;
}

sl_status_t phone_storage_set_passive_enabled(bool enabled)
{
  uint8_t val = enabled ? 1U : 0U;
  return user_eeprom_write(EEPROM_PHONE_PASSIVE_ENABLED, &val, 1U, NULL);
}

sl_status_t phone_storage_get_passive_sensitivity(uint8_t *sens)
{
  if (sens == NULL) return SL_STATUS_INVALID_PARAMETER;
  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_PASSIVE_SENSITIVITY, sens, 1U);
  if (sc != SL_STATUS_OK) { *sens = PHONE_PASSIVE_SENS_STANDARD; return sc; }
  if (*sens == 0U || *sens > PHONE_PASSIVE_SENS_FAR) { *sens = PHONE_PASSIVE_SENS_STANDARD; }
  return SL_STATUS_OK;
}

sl_status_t phone_storage_set_passive_sensitivity(uint8_t sens)
{
  return user_eeprom_write(EEPROM_PHONE_PASSIVE_SENSITIVITY, &sens, 1U, NULL);
}

sl_status_t phone_storage_get_passive_quota(uint32_t *quota)
{
  if (quota == NULL) return SL_STATUS_INVALID_PARAMETER;
  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_PASSIVE_QUOTA, quota, sizeof(uint32_t));
  if (sc != SL_STATUS_OK) { *quota = 0U; }  /* 未写入 → 0, 需 APP 刷新 */
  return sc;
}

sl_status_t phone_storage_set_passive_quota(uint32_t quota)
{
  return user_eeprom_write(EEPROM_PHONE_PASSIVE_QUOTA, &quota, sizeof(uint32_t), NULL);
}

/* ========================================================================== */
/* appCounter (U64 BE)                                                         */
/* ========================================================================== */

uint64_t phone_storage_get_app_counter(void)
{
  uint8_t buf[8];
  sl_status_t sc = user_eeprom_read(EEPROM_PHONE_APP_COUNTER, buf, 8U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ⚠️ appCounter read failed sc=0x%04lX — 返回默认值 0" USER_LOG_NL,
                  (unsigned long)sc);
    return 0ULL;
  }
  return phone_frame_read_u64_be(buf);
}

sl_status_t phone_storage_commit_app_counter(uint64_t counter)
{
  uint8_t buf[8];
  phone_frame_write_u64_be(buf, counter);
  /* 同步写入确保落盘: CTRL_COMMAND/AUTH 确认后的 appCounter 增量必须
   * 在返回前持久化, 防止断电丢失导致 APP 侧 counter 与 BG24 侧不一致 */
  return user_eeprom_write_sync(EEPROM_PHONE_APP_COUNTER, buf, 8U);
}

/* ========================================================================== */
/* 原子注册 (首次绑定) — 备用区→回读→主区→清除备用                             */
/* ========================================================================== */

sl_status_t phone_storage_atomic_register(const uint8_t public_key[65],
                                          const uint8_t key_id[16],
                                          uint32_t bind_version,
                                          const uint8_t *vin)
{
  uint8_t rdbuf65[65];
  uint8_t rdbuf16[16];
  uint8_t ver_buf[4];
  sl_status_t sc;

  if (public_key == NULL || key_id == NULL) return SL_STATUS_INVALID_PARAMETER;

  USER_LOG_INFO("[STORAGE] ===== 原子注册绑定开始 =====" USER_LOG_NL);
  USER_LOG_INFO("[STORAGE] bindVersion=%lu" USER_LOG_NL, (unsigned long)bind_version);

  /* 1. 写入备用区 (同步写入, 每个写操作落盘后才继续) */
  /*    原子操作必须同步, 不能用异步队列 */
  USER_LOG_INFO("[STORAGE] Step 1/5: 写入备用区..." USER_LOG_NL);

  sc = user_eeprom_write_sync(EEPROM_PHONE_BAK_PUBKEY, public_key, 65U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ BAK_PUBKEY write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    return sc;
  }
  USER_LOG_INFO("[STORAGE]   BAK_PUBKEY ✅" USER_LOG_NL);

  sc = user_eeprom_write_sync(EEPROM_PHONE_BAK_KEY_ID, key_id, 16U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ BAK_KEY_ID write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    user_eeprom_delete(EEPROM_PHONE_BAK_PUBKEY); return sc;
  }
  USER_LOG_INFO("[STORAGE]   BAK_KEY_ID ✅" USER_LOG_NL);

  phone_frame_write_u32_be(ver_buf, bind_version);
  sc = user_eeprom_write_sync(EEPROM_PHONE_BAK_BIND_VERSION, ver_buf, 4U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ BAK_BIND_VERSION write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    user_eeprom_delete(EEPROM_PHONE_BAK_PUBKEY);
    user_eeprom_delete(EEPROM_PHONE_BAK_KEY_ID);
    return sc;
  }
  USER_LOG_INFO("[STORAGE]   BAK_BIND_VERSION ✅" USER_LOG_NL);

  /* V1.2: 首次绑定时写入 VIN 到备用区 */
  if (vin != NULL) {
    sc = user_eeprom_write_sync(EEPROM_PHONE_BAK_VIN, vin, 17U);
    if (sc != SL_STATUS_OK) {
      USER_LOG_INFO("[STORAGE] ❌ BAK_VIN write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
      user_eeprom_delete(EEPROM_PHONE_BAK_PUBKEY);
      user_eeprom_delete(EEPROM_PHONE_BAK_KEY_ID);
      user_eeprom_delete(EEPROM_PHONE_BAK_BIND_VERSION);
      return sc;
    }
    USER_LOG_INFO("[STORAGE]   BAK_VIN ✅ (%.17s)" USER_LOG_NL, (const char *)vin);
  }

  /* 2. 回读并校验 */
  USER_LOG_INFO("[STORAGE] Step 2/5: 回读校验备用区..." USER_LOG_NL);

  memset(rdbuf65, 0, sizeof(rdbuf65));
  memset(rdbuf16, 0, sizeof(rdbuf16));
  sc = user_eeprom_read(EEPROM_PHONE_BAK_PUBKEY, rdbuf65, 65U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ BAK_PUBKEY readback failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    goto fail;
  }
  if (memcmp(rdbuf65, public_key, 65U) != 0) {
    USER_LOG_INFO("[STORAGE] ❌ BAK_PUBKEY readback MISMATCH" USER_LOG_NL);
    goto fail;
  }
  USER_LOG_INFO("[STORAGE]   BAK_PUBKEY readback ✅" USER_LOG_NL);

  sc = user_eeprom_read(EEPROM_PHONE_BAK_KEY_ID, rdbuf16, 16U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ BAK_KEY_ID readback failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    goto fail;
  }
  if (memcmp(rdbuf16, key_id, 16U) != 0) {
    USER_LOG_INFO("[STORAGE] ❌ BAK_KEY_ID readback MISMATCH" USER_LOG_NL);
    goto fail;
  }
  USER_LOG_INFO("[STORAGE]   BAK_KEY_ID readback ✅" USER_LOG_NL);

  {
    uint32_t read_ver;
    uint8_t b4[4];
    sc = user_eeprom_read(EEPROM_PHONE_BAK_BIND_VERSION, b4, 4U);
    if (sc != SL_STATUS_OK) {
      USER_LOG_INFO("[STORAGE] ❌ BAK_BIND_VERSION readback failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
      goto fail;
    }
    read_ver = phone_frame_read_u32_be(b4);
    if (read_ver != bind_version) {
      USER_LOG_INFO("[STORAGE] ❌ BAK_BIND_VERSION mismatch read=%lu expected=%lu" USER_LOG_NL,
                    (unsigned long)read_ver, (unsigned long)bind_version);
      goto fail;
    }
  }
  USER_LOG_INFO("[STORAGE]   BAK_BIND_VERSION readback ✅" USER_LOG_NL);

  /* V1.2: 回读校验 VIN */
  if (vin != NULL) {
    uint8_t rd_vin[17];
    sc = user_eeprom_read(EEPROM_PHONE_BAK_VIN, rd_vin, 17U);
    if (sc != SL_STATUS_OK) {
      USER_LOG_INFO("[STORAGE] ❌ BAK_VIN readback failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
      goto fail;
    }
    if (memcmp(rd_vin, vin, 17U) != 0) {
      USER_LOG_INFO("[STORAGE] ❌ BAK_VIN readback MISMATCH" USER_LOG_NL);
      goto fail;
    }
    USER_LOG_INFO("[STORAGE]   BAK_VIN readback ✅" USER_LOG_NL);
  }

  /* 3. 写入主区 */
  USER_LOG_INFO("[STORAGE] Step 3/5: 写入主区..." USER_LOG_NL);

  sc = user_eeprom_write_sync(EEPROM_PHONE_APP_PUBKEY, public_key, 65U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ APP_PUBKEY write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    goto fail;
  }
  USER_LOG_INFO("[STORAGE]   APP_PUBKEY ✅" USER_LOG_NL);

  sc = user_eeprom_write_sync(EEPROM_PHONE_APP_KEY_ID, key_id, 16U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ APP_KEY_ID write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    user_eeprom_delete(EEPROM_PHONE_APP_PUBKEY); goto fail;
  }
  USER_LOG_INFO("[STORAGE]   APP_KEY_ID ✅" USER_LOG_NL);

  sc = user_eeprom_write_sync(EEPROM_PHONE_BIND_VERSION, ver_buf, 4U);
  if (sc != SL_STATUS_OK) {
    USER_LOG_INFO("[STORAGE] ❌ BIND_VERSION write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
    user_eeprom_delete(EEPROM_PHONE_APP_PUBKEY);
    user_eeprom_delete(EEPROM_PHONE_APP_KEY_ID);
    goto fail;
  }
  USER_LOG_INFO("[STORAGE]   BIND_VERSION ✅" USER_LOG_NL);

  /* V1.2: 首次绑定时写入 VIN 主区 + 调用 user_vin_learn() 更新 RAM */
  if (vin != NULL) {
    USER_LOG_INFO("[STORAGE] Step 3.5/5: 写入 VIN=%.17s + learn..." USER_LOG_NL, (const char *)vin);
    sc = user_eeprom_write_sync(EEPROM_PHONE_VIN, vin, 17U);
    if (sc != SL_STATUS_OK) {
      USER_LOG_INFO("[STORAGE] ❌ VIN write failed sc=0x%04lX" USER_LOG_NL, (unsigned long)sc);
      user_eeprom_delete(EEPROM_PHONE_APP_PUBKEY);
      user_eeprom_delete(EEPROM_PHONE_APP_KEY_ID);
      user_eeprom_delete(EEPROM_PHONE_BIND_VERSION);
      goto fail;
    }
    USER_LOG_INFO("[STORAGE]   VIN ✅" USER_LOG_NL);
    /* 更新 RAM 中的 VIN 状态缓存 (刚学习时 VIU VIN 肯定匹配) */
    (void)user_vin_learn(vin);
  }

  /* 重置 appCounter = 0 */
  USER_LOG_INFO("[STORAGE] Step 4/5: 重置 appCounter + 清除备用区..." USER_LOG_NL);
  {
    uint8_t zero8[8] = {0};
    (void)user_eeprom_write_sync(EEPROM_PHONE_APP_COUNTER, zero8, 8U);
  }

  /* 4. 清除备用区 */
  user_eeprom_delete(EEPROM_PHONE_BAK_PUBKEY);
  user_eeprom_delete(EEPROM_PHONE_BAK_KEY_ID);
  user_eeprom_delete(EEPROM_PHONE_BAK_BIND_VERSION);
  if (vin != NULL) {
    user_eeprom_delete(EEPROM_PHONE_BAK_VIN);
  }

  /* 5. 更新 bindState = BOUND */
  USER_LOG_INFO("[STORAGE] Step 5/5: 更新 bindState → BOUND..." USER_LOG_NL);
  {
    uint8_t st = PHONE_DEVICE_STATE_BOUND;
    (void)user_eeprom_write(EEPROM_PHONE_BIND_STATE, &st, 1U, NULL);
  }

  USER_LOG_INFO("[STORAGE] ===== 原子注册绑定 ✅ 完成 =====" USER_LOG_NL);
  return SL_STATUS_OK;

fail:
  USER_LOG_INFO("[STORAGE] ===== 原子注册绑定 ❌ 失败, 清理备用区 =====" USER_LOG_NL);
  user_eeprom_delete(EEPROM_PHONE_BAK_PUBKEY);
  user_eeprom_delete(EEPROM_PHONE_BAK_KEY_ID);
  user_eeprom_delete(EEPROM_PHONE_BAK_BIND_VERSION);
  if (vin != NULL) {
    user_eeprom_delete(EEPROM_PHONE_BAK_VIN);
  }
  return SL_STATUS_FAIL;
}

/* ========================================================================== */
/* 原子换绑                                                                     */
/* ========================================================================== */

sl_status_t phone_storage_atomic_rebind(const uint8_t new_public_key[65],
                                        const uint8_t new_key_id[16],
                                        uint32_t new_bind_version)
{
  return phone_storage_atomic_register(new_public_key, new_key_id, new_bind_version, NULL);
}

/* ========================================================================== */
/* 存在性检查                                                                  */
/* ========================================================================== */

bool phone_storage_is_bound(void)
{
  return user_eeprom_is_valid(EEPROM_PHONE_APP_PUBKEY);
}

bool phone_storage_is_provisioned(void)
{
  return user_eeprom_is_valid(EEPROM_PHONE_BIND_SECRET);
}

/* ========================================================================== */
/* VIN 车辆识别码                                                              */
/* ========================================================================== */

sl_status_t phone_storage_get_vin(uint8_t vin[17])
{
  if (vin == NULL) return SL_STATUS_INVALID_PARAMETER;
  return user_eeprom_read(EEPROM_PHONE_VIN, vin, 17U);
}

bool phone_storage_has_vin(void)
{
  return user_eeprom_is_valid(EEPROM_PHONE_VIN);
}

sl_status_t phone_storage_clear_vin(void)
{
  return user_eeprom_delete(EEPROM_PHONE_VIN);
}
