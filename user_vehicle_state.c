/***************************************************************************//**
 * @file user_vehicle_state.c
 * @brief V1.2 车辆状态抽象层实现
 *
 * 双数据源:
 *   CAN 来源 — 由 user_app_fun 同步 RTE 信号到此模块
 *   串口来源 — 由 user_console 命令手动设置 (override 模式)
 *
 * 串口 override 激活时, 所有 getter 返回手动注入值;
 * 串口 override 关闭时, 返回 CAN 来源值。
 *
 * 后续 CAN 矩阵确定后, 只需实现 CAN→内部状态的同步逻辑。
 ******************************************************************************/

#include "user_vehicle_state.h"
#include "user_log_console.h"
#include "sl_sleeptimer.h"
#include <string.h>

/* ========================================================================== */
/* 内部状态                                                                   */
/* ========================================================================== */

/* CAN 数据源 (正常运行时由 user_app_fun 同步) */
static struct {
  uint8_t  vin[17];
  bool     vin_valid;
  uint32_t vin_last_update_ms;

  bool     peps_key_in_car;
  uint8_t  ignition_gear;
  uint16_t remaining_range_km;
  uint8_t  door_status;
  uint8_t  final_lock_state;
} g_can_state;

/* 串口 override 数据源 */
static struct {
  bool     active;
  uint8_t  vin[17];
  bool     vin_set;

  bool     peps_key_in_car;
  uint8_t  ignition_gear;
  uint16_t remaining_range_km;
  uint8_t  door_status;
  uint8_t  final_lock_state;
} g_override_state;

static bool g_init_done = false;

/* ========================================================================== */
/* 时间戳 (ms) — 简单实现, 上层提供更精确的时间源                          */
/* ========================================================================== */

/* 由 vehicle_state_process() 传入或使用 sleeptimer */
static uint32_t g_now_ms = 0;

/* ========================================================================== */
/* 公开 API — VIN                                                             */
/* ========================================================================== */

bool vehicle_state_is_vin_available(void)
{
  /* 串口模式: 直接返回是否设置了 VIN */
  if (g_override_state.active && g_override_state.vin_set) {
    return true;
  }

  /* CAN 模式: 检查超时 */
  if (!g_can_state.vin_valid) return false;
  if (g_now_ms - g_can_state.vin_last_update_ms > VIN_TIMEOUT_MS) {
    return false;
  }
  return true;
}

sl_status_t vehicle_state_get_vin(uint8_t vin[17])
{
  if (vin == NULL) return SL_STATUS_INVALID_PARAMETER;

  /* 串口模式 */
  if (g_override_state.active && g_override_state.vin_set) {
    memcpy(vin, g_override_state.vin, 17U);
    return SL_STATUS_OK;
  }

  /* CAN 模式 */
  if (!g_can_state.vin_valid) return SL_STATUS_NOT_READY;
  if (g_now_ms - g_can_state.vin_last_update_ms > VIN_TIMEOUT_MS) {
    return SL_STATUS_TIMEOUT;
  }
  memcpy(vin, g_can_state.vin, 17U);
  return SL_STATUS_OK;
}

void vehicle_state_set_vin_override(const uint8_t vin[17])
{
  if (vin == NULL) return;
  memcpy(g_override_state.vin, vin, 17U);
  g_override_state.vin_set = true;
  USER_LOG_INFO("[VEH_STATE] VIN override: %.17s" USER_LOG_NL, (const char *)vin);
}

void vehicle_state_clear_vin_override(void)
{
  memset(g_override_state.vin, 0, 17U);
  g_override_state.vin_set = false;
  USER_LOG_INFO("[VEH_STATE] VIN override 已清除" USER_LOG_NL);
}

/* ========================================================================== */
/* 公开 API — 车辆状态                                                        */
/* ========================================================================== */

bool vehicle_state_is_peps_key_in_car(void)
{
  if (g_override_state.active) return g_override_state.peps_key_in_car;
  return g_can_state.peps_key_in_car;
}

uint8_t vehicle_state_get_ignition_gear(void)
{
  if (g_override_state.active) return g_override_state.ignition_gear;
  return g_can_state.ignition_gear;
}

uint16_t vehicle_state_get_remaining_range_km(void)
{
  if (g_override_state.active) return g_override_state.remaining_range_km;
  return g_can_state.remaining_range_km;
}

uint8_t vehicle_state_get_door_status(void)
{
  if (g_override_state.active) return g_override_state.door_status;
  return g_can_state.door_status;
}

uint8_t vehicle_state_get_final_lock_state(void)
{
  if (g_override_state.active) return g_override_state.final_lock_state;
  return g_can_state.final_lock_state;
}

void vehicle_state_set_override(uint8_t ignition, uint16_t range,
                                uint8_t doors, uint8_t lock_state, bool peps_key)
{
  g_override_state.active = true;
  g_override_state.ignition_gear      = ignition;
  g_override_state.remaining_range_km = range;
  g_override_state.door_status        = doors;
  g_override_state.final_lock_state   = lock_state;
  g_override_state.peps_key_in_car    = peps_key;

  USER_LOG_INFO("[VEH_STATE] Override: ign=%u range=%ukm doors=0x%02X lock=%u peps=%u" USER_LOG_NL,
                (unsigned)ignition, (unsigned)range,
                (unsigned)doors, (unsigned)lock_state, (unsigned)peps_key);
}

void vehicle_state_clear_override(void)
{
  g_override_state.active = false;
  USER_LOG_INFO("[VEH_STATE] Override 已清除, 恢复 CAN 来源" USER_LOG_NL);
}

bool vehicle_state_is_override_active(void)
{
  return g_override_state.active;
}

/* ========================================================================== */
/* 内部接口: CAN→状态同步 (供 user_app_fun 调用, 在 user_vehicle_state.h 中声明) */
/* ========================================================================== */

/** CAN 侧 VIN 数据更新 (由 user_app_fun 的 CAN RX 回调调用) */
void vehicle_state_update_vin_from_can(const uint8_t *vin_data, uint8_t len, bool valid)
{
  if (valid && len >= 17U) {
    bool was_valid = g_can_state.vin_valid;
    memcpy(g_can_state.vin, vin_data, 17U);
    g_can_state.vin_valid = true;
    g_can_state.vin_last_update_ms = g_now_ms;
    if (!was_valid) {
      USER_LOG_INFO("[VEH_STATE] VIN 首次接收: %.17s" USER_LOG_NL, (const char *)g_can_state.vin);
    }
  } else {
    g_can_state.vin_valid = false;
  }
}

/** CAN 侧车辆状态更新 */
void vehicle_state_update_from_can(uint8_t ignition, uint16_t range,
                                   uint8_t doors, uint8_t lock_state, bool peps_key)
{
  g_can_state.ignition_gear      = ignition;
  g_can_state.remaining_range_km = range;
  g_can_state.door_status        = doors;
  g_can_state.final_lock_state   = lock_state;
  g_can_state.peps_key_in_car    = peps_key;
}

/* ========================================================================== */
/* 生命周期                                                                   */
/* ========================================================================== */

void vehicle_state_init(void)
{
  memset(&g_can_state, 0, sizeof(g_can_state));
  memset(&g_override_state, 0, sizeof(g_override_state));
  g_can_state.final_lock_state = FINAL_LOCK_UNKNOWN;
  g_init_done = true;
  USER_LOG_INFO("[VEH_STATE] 初始化完成 (CAN 来源默认值, override=off)" USER_LOG_NL);
}

void vehicle_state_process(void)
{
  if (!g_init_done) return;

  /* 更新当前时间 (供 VIN 超时检测使用) */
  {
    uint64_t freq = sl_sleeptimer_get_timer_frequency();
    uint64_t tick = sl_sleeptimer_get_tick_count64();
    g_now_ms = (uint32_t)((tick * 1000ULL) / freq);
  }

  /* 检查 VIN 超时 (CAN 模式下) */
  if (!g_override_state.active && g_can_state.vin_valid) {
    if (g_now_ms - g_can_state.vin_last_update_ms > VIN_TIMEOUT_MS) {
      g_can_state.vin_valid = false;
      USER_LOG_INFO("[VEH_STATE] VIN 超时 (>%ums) → 标记无效" USER_LOG_NL,
                    (unsigned)VIN_TIMEOUT_MS);
    }
  }
}
