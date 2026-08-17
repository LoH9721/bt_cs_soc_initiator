/***************************************************************************//**
 * @file user_vin.c
 * @brief V1.2 VIN 车辆关联状态机实现
 *
 * 职责:
 *   - 管理 VIN 四态: UNLEARNED / MATCH / MISMATCH / UNKNOWN
 *   - 上电时从 EEPROM 加载本地 VIN, 初始状态为 UNKNOWN (等待 VIU VIN 到达)
 *   - 定期比对本地 VIN vs VIU 当前 VIN, 输出状态变化日志
 *   - 提供抽象关联结果给 APP (仅 normal/abnormal/unconfirmed, 不暴露 VIN)
 *
 * 不直接访问 CAN / RTE — 通过 vehicle_state_get_vin() 获取 VIU VIN
 ******************************************************************************/

#include "user_vin.h"
#include "user_phone/data/phone_storage.h"
#include "user_phone/phone_cfg.h"
#include "user_vehicle_state.h"
#include "user_log_console.h"
#include <string.h>

/* ========================================================================== */
/* 内部状态                                                                   */
/* ========================================================================== */

static uint8_t  g_vin_state       = VIN_STATE_UNLEARNED;
static uint8_t  g_local_vin[17];   /* 本地绑定的 VIN (EEPROM 缓存) */
static bool     g_vin_learned     = false;
static bool     g_init_done       = false;
static uint32_t g_last_refresh_ms = 0;

/* 前向声明: vehicle_state 接口 (定义在 user_vehicle_state.h) */
extern bool vehicle_state_is_vin_available(void);
extern sl_status_t vehicle_state_get_vin(uint8_t vin[17]);

/* ========================================================================== */
/* 公开 API                                                                   */
/* ========================================================================== */

void user_vin_init(void)
{
  sl_status_t sc;

  memset(g_local_vin, 0, sizeof(g_local_vin));

  /* 从 EEPROM 加载本地 VIN */
  sc = phone_storage_get_vin(g_local_vin);
  if (sc == SL_STATUS_OK && phone_storage_has_vin()) {
    g_vin_learned = true;
    g_vin_state = VIN_STATE_UNKNOWN;  /* 等待 VIU VIN 到达后比对 */
    USER_LOG_INFO("[VIN] 上电初始化: 已学习 VIN, 状态=UNKNOWN (等待VIU VIN)" USER_LOG_NL);
    USER_LOG_INFO("[VIN]   本地 VIN: %.17s" USER_LOG_NL, (const char *)g_local_vin);
  } else {
    g_vin_learned = false;
    g_vin_state = VIN_STATE_UNLEARNED;
    USER_LOG_INFO("[VIN] 上电初始化: VIN 未学习" USER_LOG_NL);
  }

  g_last_refresh_ms = 0;
  g_init_done = true;
}

void user_vin_process(void)
{
  if (!g_init_done) return;

  /* 未学习 VIN 则跳过比对 */
  if (!g_vin_learned) return;

  /* 检查 VIU VIN 是否可用 */
  if (!vehicle_state_is_vin_available()) {
    /* VIU VIN 不可用时状态为 UNKNOWN */
    if (g_vin_state != VIN_STATE_UNKNOWN) {
      g_vin_state = VIN_STATE_UNKNOWN;
      USER_LOG_INFO("[VIN] 状态变化: → UNKNOWN (VIU VIN 不可用)" USER_LOG_NL);
    }
    return;
  }

  /* 读取 VIU 当前 VIN 进行比对 */
  uint8_t viu_vin[17];
  sl_status_t sc = vehicle_state_get_vin(viu_vin);
  if (sc != SL_STATUS_OK) {
    if (g_vin_state != VIN_STATE_UNKNOWN) {
      g_vin_state = VIN_STATE_UNKNOWN;
      USER_LOG_INFO("[VIN] 状态变化: → UNKNOWN (读取VIU VIN失败)" USER_LOG_NL);
    }
    return;
  }

  /* 比对本地 VIN vs VIU VIN */
  uint8_t prev_state = g_vin_state;
  if (memcmp(g_local_vin, viu_vin, 17U) == 0) {
    g_vin_state = VIN_STATE_MATCH;
  } else {
    g_vin_state = VIN_STATE_MISMATCH;
  }

  if (g_vin_state != prev_state) {
    USER_LOG_INFO("[VIN] 状态变化: %s → %s" USER_LOG_NL,
                  user_vin_state_name(prev_state),
                  user_vin_state_name(g_vin_state));
    if (g_vin_state == VIN_STATE_MISMATCH) {
      USER_LOG_INFO("[VIN]   本地: %.17s" USER_LOG_NL, (const char *)g_local_vin);
      USER_LOG_INFO("[VIN]   VIU:  %.17s" USER_LOG_NL, (const char *)viu_vin);
    }
  }
}

sl_status_t user_vin_learn(const uint8_t vin[17])
{
  if (vin == NULL) return SL_STATUS_INVALID_PARAMETER;

  /* 校验 VIN 基本有效性 */
  {
    bool all_zero = true;
    bool all_ff   = true;
    for (int i = 0; i < 17; i++) {
      if (vin[i] != 0x00) all_zero = false;
      if (vin[i] != 0xFF) all_ff   = false;
    }
    if (all_zero || all_ff) {
      USER_LOG_INFO("[VIN] learn 失败: VIN 无效 (全0或全FF)" USER_LOG_NL);
      return SL_STATUS_INVALID_PARAMETER;
    }
  }

  /* 不允许重复学习 */
  if (g_vin_learned) {
    USER_LOG_INFO("[VIN] learn 失败: 已学习 VIN, 不允许覆盖" USER_LOG_NL);
    return SL_STATUS_ALREADY_INITIALIZED;
  }

  /* 写入 EEPROM (实际写入由 phone_storage_atomic_register_v12 完成,
   * 此函数仅更新 RAM 缓存) */
  memcpy(g_local_vin, vin, 17U);
  g_vin_learned = true;
  g_vin_state = VIN_STATE_MATCH;  /* 刚学习时 VIU VIN 肯定匹配 */

  USER_LOG_INFO("[VIN] ✅ 学习成功: %.17s" USER_LOG_NL, (const char *)g_local_vin);
  return SL_STATUS_OK;
}

uint8_t user_vin_get_state(void)
{
  return g_vin_state;
}

bool user_vin_is_match(void)
{
  return (g_vin_state == VIN_STATE_MATCH);
}

bool user_vin_is_learned(void)
{
  return g_vin_learned;
}

uint8_t user_vin_get_abstract_status(void)
{
  switch (g_vin_state) {
    case VIN_STATE_MATCH:     return VIN_ASSOC_NORMAL;
    case VIN_STATE_MISMATCH:  return VIN_ASSOC_ABNORMAL;
    case VIN_STATE_UNKNOWN:   return VIN_ASSOC_UNCONFIRMED;
    case VIN_STATE_UNLEARNED:
    default:                  return VIN_ASSOC_ABNORMAL;
  }
}

sl_status_t user_vin_get_local_vin(uint8_t vin[17])
{
  if (vin == NULL) return SL_STATUS_INVALID_PARAMETER;
  if (!g_vin_learned) return SL_STATUS_NOT_FOUND;
  memcpy(vin, g_local_vin, 17U);
  return SL_STATUS_OK;
}

sl_status_t user_vin_clear(void)
{
  memset(g_local_vin, 0, sizeof(g_local_vin));
  g_vin_learned = false;
  g_vin_state = VIN_STATE_UNLEARNED;
  USER_LOG_INFO("[VIN] 已清除本地 VIN" USER_LOG_NL);
  return phone_storage_clear_vin();
}

const char *user_vin_state_name(uint8_t state)
{
  switch (state) {
    case VIN_STATE_UNLEARNED: return "UNLEARNED";
    case VIN_STATE_MATCH:     return "MATCH";
    case VIN_STATE_MISMATCH:  return "MISMATCH";
    case VIN_STATE_UNKNOWN:   return "UNKNOWN";
    default:                  return "?";
  }
}
