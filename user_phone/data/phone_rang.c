/***************************************************************************//**
 * @file phone_rang.c
 * @brief 手机 RSSI 距离估算模块实现
 *
 * 对数路径损耗模型: distance = 10 ^ ((tx_power_1m - rssi) / (10 * n))
 *   - tx_power_1m: 1m 处参考 RSSI (dBm), 可标定 (NVM 持久化)
 *   - n: 路径损耗指数 (自由空间 2.0, 室内 2.5~3.5, 遮挡 4.0+), 可标定
 *
 * 本端 RSSI 通过 sl_bt_connection_get_median_rssi 周期获取，
 * 远端 RSSI 由 phone_cmd 解析 0x12 帧后喂入。
 * 双源 1D 卡尔曼滤波融合: 本端优先，远端作为补充。
 ******************************************************************************/

#include "sl_bt_api.h"
#include "sl_sleeptimer.h"
#include "user_log_console.h"
#include "user_eeprom/user_eeprom.h"
#include "user_phone/data/phone_rang.h"
/* 连接状态通过 phone_comm 门面查询 */
#include "user_phone/phone_comm.h"
#include "user_phone/phone_cfg.h"

/* ========== 编译期默认值 (NVM 无效时的回退值, 宏定义见 phone_rang.h) ========== */
#define PHONE_RANG_POLL_MS                   300U
#define PHONE_RANG_LOCAL_TIMEOUT_MS          3000U
#define PHONE_RANG_LN10                      2.302585093f  /* ln(10) */
#define PHONE_RANG_INVALID_DIST_M            999.0f  /* 未连接时的哨兵值 */

/* ========== 卡尔曼滤波参数 ========== */
#define PHONE_RANG_KF_Q  0.06f  /* 过程噪声: 覆盖慢走 (300ms 内移动 ~0.2m) */
#define PHONE_RANG_KF_R  2.0f   /* 测量噪声: BLE RSSI 在 1~5m 范围的典型距离方差 */

/* ========== 内部: 10^x (不依赖 math.h, newlib-nano powf 在 ARM32 上有 bug) ========== */
static float pow10f(float x)
{
  /* 10^x = e^(y), y = x * ln(10)
   * Taylor: e^y ≈ 1 + y + y²/2! + y³/3! + y⁴/4! + y⁵/5! + y⁶/6!
   * 在 RSSI 典型范围的指数 [-4, +3] 内误差 < 2% */
  float y = x * PHONE_RANG_LN10;
  float y2 = y * y;
  float y3 = y2 * y;
  float y4 = y3 * y;
  float y5 = y4 * y;
  float y6 = y5 * y;
  return 1.0f + y
       + y2 * 0.5f
       + y3 * 0.16666667f
       + y4 * 0.041666667f
       + y5 * 0.008333333f
       + y6 * 0.001388889f;
}

/* ========== 运行时校准参数 (init 时从 NVM 加载, 无效时使用编译期默认值) ========== */
static int8_t g_cal_rssi_1m  = PHONE_RANG_DEFAULT_RSSI_1M;
static int8_t g_cal_rssi_10m = PHONE_RANG_DEFAULT_RSSI_10M;
static float  g_cal_path_loss_n = PHONE_RANG_DEFAULT_PATH_LOSS_N;

/* ========== 状态 ========== */
static float    g_dist_remote_m;
static int8_t   g_rssi_remote;
static bool     g_remote_valid;

static float    g_dist_local_m;
static int8_t   g_rssi_local;
static bool     g_local_valid;
static uint32_t g_local_last_ms;
static uint64_t g_poll_next_ms;

/* 1D 卡尔曼滤波状态 (替代 EMA) */
static float    g_kf_x;        /* 状态: 估计距离 (m) */
static float    g_kf_p;        /* 估计协方差 */
static bool     g_kf_init;     /* 卡尔曼是否已初始化 */
static bool     g_fused_valid;
static uint64_t g_fused_last_ms;

/* ========== 内部: 1D 卡尔曼滤波 (恒定位置模型) ========== */
static void rang_kf_update(float z_meas_m)
{
  /* Predict: 位置不变, 协方差增长 */
  float p_pred = g_kf_p + PHONE_RANG_KF_Q;

  /* Update: 计算卡尔曼增益, 融合测量值 */
  float k = p_pred / (p_pred + PHONE_RANG_KF_R);

  if (!g_kf_init) {
    /* 首次测量: 直接信任测量值, 协方差从较大值开始收敛 */
    g_kf_x    = z_meas_m;
    g_kf_p    = 1.0f;
    g_kf_init = true;
  } else {
    g_kf_x = g_kf_x + k * (z_meas_m - g_kf_x);
    g_kf_p = (1.0f - k) * p_pred;
  }
  g_fused_valid = true;
  g_fused_last_ms = sl_sleeptimer_tick_to_ms(
                      sl_sleeptimer_get_tick_count64());
}

/* ========== 内部: RSSI → 距离 (使用运行时校准参数) ========== */
static float rang_rssi_to_m(int8_t rssi)
{
  float exponent = (float)(g_cal_rssi_1m - rssi)
                 / (10.0f * g_cal_path_loss_n);
  return pow10f(exponent);
}

/* ========== 内部: 本端 RSSI 轮询 ========== */
static void rang_poll_local(void)
{
  uint8_t conn = phone_comm_connection_handle_get();
  if (conn == SL_BT_INVALID_CONNECTION_HANDLE) return;

  int8_t rssi = 0;
  if (sl_bt_connection_get_median_rssi(conn, &rssi) != SL_STATUS_OK) return;

  g_rssi_local      = rssi;
  g_local_valid     = true;
  g_dist_local_m    = rang_rssi_to_m(rssi);
  g_local_last_ms   = (uint32_t)sl_sleeptimer_tick_to_ms(
                        sl_sleeptimer_get_tick_count64());

  rang_kf_update(g_dist_local_m);
}

/* ========== 内部: 校准加载 (NVM 存 rssi_1m/rssi_10m, 内部反算 n) ========== */
static void rang_load_cal_from_nvm(void)
{
  /* rssi_1m: NVM 有效则用 NVM, 否则用默认值 */
  int8_t r1 = PHONE_RANG_DEFAULT_RSSI_1M;
  if (user_eeprom_is_valid(EEPROM_PHONE_RANG_CAL_RSSI_1M)) {
    user_eeprom_read(EEPROM_PHONE_RANG_CAL_RSSI_1M, &r1, 1U);
  }
  g_cal_rssi_1m = r1;

  /* rssi_10m: NVM 有效则用 NVM, 否则用默认值 */
  int8_t r10 = PHONE_RANG_DEFAULT_RSSI_10M;
  if (user_eeprom_is_valid(EEPROM_PHONE_RANG_CAL_RSSI_10M)) {
    user_eeprom_read(EEPROM_PHONE_RANG_CAL_RSSI_10M, &r10, 1U);
  }
  g_cal_rssi_10m = r10;

  /* 内部反算 n = (rssi_1m - rssi_10m) / 10, 钳位 n ∈ [1.0, 6.0] */
  int16_t n_x10 = (int16_t)r1 - (int16_t)r10;
  if (n_x10 < 10)      g_cal_path_loss_n = 1.00f;
  else if (n_x10 > 60) g_cal_path_loss_n = 6.00f;
  else                 g_cal_path_loss_n = (float)n_x10 / 10.0f;
}

/* ========== Public API ========== */

void phone_rang_init(void)
{
  g_dist_remote_m  = 0.0f;
  g_rssi_remote    = 0;
  g_remote_valid   = false;

  g_dist_local_m   = 0.0f;
  g_rssi_local     = 0;
  g_local_valid    = false;
  g_poll_next_ms   = 0;
  g_local_last_ms  = 0;

  g_kf_x           = PHONE_RANG_INVALID_DIST_M;
  g_kf_p           = 1.0f;
  g_kf_init        = false;
  g_fused_valid    = false;
  g_fused_last_ms  = 0ULL;

  /* 从 NVM 加载校准参数 (若 NVM 无效则使用默认值, 定义见 phone_rang.h) */
  rang_load_cal_from_nvm();
  {
    bool r1_nvm  = user_eeprom_is_valid(EEPROM_PHONE_RANG_CAL_RSSI_1M);
    bool r10_nvm = user_eeprom_is_valid(EEPROM_PHONE_RANG_CAL_RSSI_10M);
    int n_x10 = (int)(g_cal_path_loss_n * 10.0f + 0.5f);
    USER_LOG_INFO("[PHONE_RANG] init: rssi_1m=%d(%s) rssi_10m=%d(%s) n=%d.%d pol=%dms" USER_LOG_NL,
                  (int)g_cal_rssi_1m,  r1_nvm  ? "NVM" : "DEFAULT",
                  (int)g_cal_rssi_10m, r10_nvm ? "NVM" : "DEFAULT",
                  n_x10 / 10, n_x10 % 10,
                  (int)PHONE_RANG_POLL_MS);
  }
}

void phone_rang_process_action(void)
{
  if (!phone_comm_is_connected()) return;

  uint64_t now = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count64());
  if (now >= g_poll_next_ms) {
    g_poll_next_ms = now + (uint64_t)PHONE_RANG_POLL_MS;
    rang_poll_local();
  }

#ifdef PHONE_RANG_RSSI_LOG
  /* 诊断: 每 5s 打一次本端 RSSI (确认手机距离/ACL 存活), 默认关 */
  {
    static uint64_t rssi_log_next_ms = 0;
    if (now >= rssi_log_next_ms) {
      rssi_log_next_ms = now + 5000ULL;
      USER_LOG_INFO("[PHONE_RANG] rssi_local=%d dist=%.2fm valid=%s" USER_LOG_NL,
                    (int)g_rssi_local, (double)g_dist_local_m,
                    g_local_valid ? "Y" : "N");
    }
  }
#endif
}

void phone_rang_feed_remote_rssi(int8_t rssi)
{
  g_rssi_remote    = rssi;
  g_remote_valid   = true;
  g_dist_remote_m  = rang_rssi_to_m(rssi);

  USER_LOG_DEBUG("[PHONE_RANG] remote RSSI: %d dBm -> %d cm" USER_LOG_NL,
                 rssi, (int)(g_dist_remote_m * 100.0f + 0.5f));

  /* 若本端过期，用远端补充卡尔曼滤波 */
  uint32_t now = (uint32_t)sl_sleeptimer_tick_to_ms(
                   sl_sleeptimer_get_tick_count64());
  if (!g_local_valid
      || (now - g_local_last_ms) > PHONE_RANG_LOCAL_TIMEOUT_MS) {
    rang_kf_update(g_dist_remote_m);
  }
}

void phone_rang_reset(void)
{
  g_remote_valid    = false;
  g_local_valid     = false;
  g_kf_init         = false;
  g_kf_x            = PHONE_RANG_INVALID_DIST_M;
  g_fused_valid     = false;
  g_fused_last_ms   = 0ULL;
  g_poll_next_ms    = 0;
}

bool phone_rang_get_distance(float *dist_m)
{
  if (dist_m == NULL || !phone_rang_is_valid()) return false;
  *dist_m = g_kf_x;
  return true;
}

bool phone_rang_is_valid(void)
{
  uint64_t now;

  if (!phone_comm_is_connected() || !g_fused_valid) return false;

  now = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count64());
  return (now - g_fused_last_ms) <= (uint64_t)PHONE_RANG_FRESH_TIMEOUT_MS;
}

bool phone_rang_get_local_rssi(int8_t *rssi)
{
  if (rssi == NULL || !g_local_valid) return false;
  *rssi = g_rssi_local;
  return true;
}

bool phone_rang_get_remote_rssi(int8_t *rssi)
{
  if (rssi == NULL || !g_remote_valid) return false;
  *rssi = g_rssi_remote;
  return true;
}

/* ========== 校准参数管理 ========== */

void phone_rang_reload_calibration(void)
{
  int n_x10;
  rang_load_cal_from_nvm();

  n_x10 = (int)(g_cal_path_loss_n * 10.0f + 0.5f);
  USER_LOG_INFO("[PHONE_RANG] cal reload: rssi_1m=%d rssi_10m=%d n=%d.%d" USER_LOG_NL,
                (int)g_cal_rssi_1m, (int)g_cal_rssi_10m, n_x10 / 10, n_x10 % 10);
}

void phone_rang_get_calibration(int8_t *rssi_1m, int8_t *rssi_10m, float *path_loss_n)
{
  if (rssi_1m    != NULL) *rssi_1m    = g_cal_rssi_1m;
  if (rssi_10m   != NULL) *rssi_10m   = g_cal_rssi_10m;
  if (path_loss_n != NULL) *path_loss_n = g_cal_path_loss_n;
}

void phone_rang_clear_calibration(void)
{
  user_eeprom_delete(EEPROM_PHONE_RANG_CAL_RSSI_1M);
  user_eeprom_delete(EEPROM_PHONE_RANG_CAL_RSSI_10M);
  rang_load_cal_from_nvm();

  USER_LOG_INFO("[PHONE_RANG] cal cleared, reverted to defaults" USER_LOG_NL);
}

void phone_rang_cal_auto(int8_t rssi_1m, int8_t rssi_10m)
{
  /* n = (rssi_1m - rssi_10m) / (10 * log10(10)) = (rssi_1m - rssi_10m) / 10 */
  int16_t n_x10 = (int16_t)rssi_1m - (int16_t)rssi_10m;

  if (n_x10 <= 0) {
    USER_LOG_ERROR("[PHONE_RANG] cal_auto: invalid — rssi_1m(%d) <= rssi_10m(%d), expect 10m weaker" USER_LOG_NL,
                   (int)rssi_1m, (int)rssi_10m);
    return;
  }

  /* 钳位 n ∈ [1.0, 6.0] */
  if (n_x10 < 10)      n_x10 = 10;
  else if (n_x10 > 60) n_x10 = 60;

  /* NVM 存原始 RSSI 值, 内部反算 n */
  user_eeprom_write(EEPROM_PHONE_RANG_CAL_RSSI_1M, &rssi_1m, 1U, NULL);
  g_cal_rssi_1m = rssi_1m;

  user_eeprom_write(EEPROM_PHONE_RANG_CAL_RSSI_10M, &rssi_10m, 1U, NULL);
  g_cal_rssi_10m = rssi_10m;

  g_cal_path_loss_n = (float)n_x10 / 10.0f;

  USER_LOG_INFO("[PHONE_RANG] cal_auto: rssi_1m=%d rssi_10m=%d → n=%d.%d (NVM written)" USER_LOG_NL,
                (int)rssi_1m, (int)rssi_10m, (int)(n_x10 / 10), (int)(n_x10 % 10));
}
