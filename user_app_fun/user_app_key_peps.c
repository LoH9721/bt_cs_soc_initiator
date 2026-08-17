/***************************************************************************//**
 * @file user_app_key_peps.c
 * @brief PEPS 模块: RTT+RSSI 融合测距 → 钥匙位置区判断 → 自动解闭锁
 *
 * 数据流:
 *   cs_key_rang (RTT) + key_rssi_rang (RSSI) → EMA滤波 → 融合距离
 *   → 方向迟滞判断 zone → zone 跳变检测 → one-shot 自动命令 (+2s冷却)
 *
 * 多钥匙预留: 内部 key_peps_state_t 数组, 最多 4 把钥匙。
 *              主钥匙 = fused_valid 中距离最小的那把。
 ******************************************************************************/
#include "user_app_key_peps.h"
#include "app.h"
#include "cs_key_rang/cs_key_rang.h"
#include "key_connect/key_gatt_rang.h"
#include "key_connect/key_connect.h"
#include "key_connect/key_gatt_cmd.h"
#include "user_can_common/CanMatrix/CanMatrix_Cfg.h"
#include "user_log_console.h"
#include "sl_sleeptimer.h"
#include <string.h>
#include <math.h>


/* ========== 融合滤波常量 ================================= */
#define RTT_TIMEOUT_MS       2500U
#define LIKELINESS_MIN       0.5f
#define RTT_EMA_ALPHA        0.4f
#define RSSI_EMA_ALPHA       0.15f

/* ========== 迟滞阈值 (米) ================================= */
#define TH_IN_CAR__ENTER      1.5f   /* 进入车内的距离 */
#define TH_IN_CAR__EXIT       2.5f   /* 退出车内的距离 */
#define TH_UNLOCK_ENTER_FROM_LOCK   5.0f   /* 从闭锁区进入解锁区 */
#define TH_UNLOCK_EXIT_TO_LOCK      8.0f   /* 从解锁区退出到闭锁区 */
#define TH_LOCK_ENTER_FROM_INVALID  15.0f  /* 从无效区进入闭锁区 */
#define TH_LOCK_EXIT_TO_INVALID     18.0f  /* 从闭锁区退出到无效区 */

/* ========== 防抖与冷却 =================================== */
#define ZONE_DEBOUNCE_COUNT   3
#define AUTO_CMD_COOLDOWN_MS  2000U


/* ========== 数据类型 ===================================== */

typedef struct {
    uint8_t  conn_handle;          /* BLE connection handle */
    bool     active;               /* 槽位有效 */

    /* RTT 状态 */
    float    rtt_filtered_m;       /* RTL 滤波后的 RTT 距离 */
    uint32_t rtt_last_ms;          /* 最后一次有效 RTT 的时间戳 */
    bool     rtt_has_data;         /* 曾收到过有效 RTT */

    /* RSSI 状态 */
    float    rssi_dist_m;          /* 最新的 RSSI 距离 (优选 local, 其次 remote) */
    bool     rssi_valid;           /* RSSI 数据有效 */

    /* 融合滤波 */
    float    ema_distance_m;       /* EMA 滤波后的融合距离 */
    bool     ema_init_done;        /* EMA 是否已被初始化 */
    bool     fused_valid;          /* 融合距离是否有效 */

    /* Zone 状态 */
    uint8_t  current_zone;         /* 当前 zone (PEPS_ZONE_*) */
    uint8_t  pending_zone;         /* 待确认的 zone */
    uint8_t  debounce_cnt;         /* 连续确认次数 */
    bool     zone_init_done;       /* 首次 zone 确定完成 */
} key_peps_state_t;


/* ========== 模块静态数据 ================================= */

static key_peps_state_t g_keys[USER_APP_KEY_PEPS_MAX_KEYS];
static uint8_t          g_active_count;
static int              g_primary_idx;          /* -1 = 无有效钥匙 */
static uint8_t          g_auto_cmd_pending;     /* one-shot 命令 */
static uint32_t         g_cmd_cooldown_ms;      /* 冷却期截止时间 */


/* ========== 内部: EMA 滤波 =============================== */
static float peps_ema(float new_val, float prev_ema, float alpha)
{
    if (prev_ema < 0.01f && prev_ema > -0.01f) {
        return new_val;  /* 首次初始化, 直接用新值 */
    }
    return alpha * new_val + (1.0f - alpha) * prev_ema;
}


/* ========== 内部: peps_zone → AppProto_Zone_Type 映射 ==== */
static uint8_t zone_to_appproto(uint8_t peps_zone)
{
    switch (peps_zone) {
    case PEPS_ZONE_IN_CAR:           return (uint8_t)APP_PROTO_ZONE_IN_CAR;
    case PEPS_ZONE_OUTSIDE_UNLOCK:   return (uint8_t)APP_PROTO_ZONE_OUTSIDE_UNLOCK;
    case PEPS_ZONE_OUTSIDE_LOCK:     return (uint8_t)APP_PROTO_ZONE_OUTSIDE_LOCK;
    case PEPS_ZONE_OUTSIDE_INVALID:  return (uint8_t)APP_PROTO_ZONE_PARKING_INVALID;
    default:                         return (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
    }
}


/* ========== 内部: RTT+RSSI 融合 =========================== */
static void peps_fuse_distance(key_peps_state_t *key)
{
    uint64_t now_ms = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count64());
    bool rtt_fresh = false;
    float raw_dist = 0.0f;
    float alpha;

    /* 1. RTT 获取 */
    uint8_t conn = app_key_conn_handle;
    if (conn != SL_BT_INVALID_CONNECTION_HANDLE && cs_key_rang_has_new_result(conn)) {
        cs_key_rang_result_t rtt_res;
        if (cs_key_rang_get_result(conn, &rtt_res) == SL_STATUS_OK) {
            /* NaN 检查: NaN != NaN */
            if (rtt_res.distance_filtered_m == rtt_res.distance_filtered_m
                && rtt_res.likeliness >= LIKELINESS_MIN) {
                key->rtt_filtered_m = rtt_res.distance_filtered_m;
                key->rtt_last_ms    = (uint32_t)now_ms;
                key->rtt_has_data   = true;
            }
        }
    }

    /* 2. RSSI 获取 */
    {
        key_rssi_rang_result_t rssi_res;
        if (key_rssi_rang_get_result(&rssi_res)) {
            if (rssi_res.local_valid) {
                key->rssi_dist_m = rssi_res.dist_local_m;
                key->rssi_valid  = true;
            } else if (rssi_res.remote_valid) {
                key->rssi_dist_m = rssi_res.dist_remote_m;
                key->rssi_valid  = true;
            }
        }
    }

    /* 3. 选择数据源: RTT 新鲜 → RTT; 否则 → RSSI */
    rtt_fresh = key->rtt_has_data
             && ((uint32_t)now_ms - key->rtt_last_ms) < RTT_TIMEOUT_MS;

    if (rtt_fresh) {
        raw_dist = key->rtt_filtered_m;
        alpha    = RTT_EMA_ALPHA;
    } else if (key->rssi_valid) {
        /* RTT→RSSI 接力: 如果 EMA 已有值则不重新初始化 */
        if (!key->ema_init_done && key->rtt_has_data) {
            /* 用最后一个 RTT 值初始化 EMA, 避免跳变 */
            key->ema_distance_m = key->rtt_filtered_m;
            key->ema_init_done  = true;
        }
        raw_dist = key->rssi_dist_m;
        alpha    = RSSI_EMA_ALPHA;
    } else {
        key->fused_valid = false;
        return;
    }

    /* 4. EMA 滤波 */
    if (!key->ema_init_done) {
        key->ema_distance_m = raw_dist;
        key->ema_init_done  = true;
    } else {
        key->ema_distance_m = peps_ema(raw_dist, key->ema_distance_m, alpha);
    }
    key->fused_valid = true;
}


/* ========== 内部: 方向迟滞 zone 判断 ====================== */
static uint8_t peps_determine_zone(uint8_t current_zone, float dist_m,
                                    bool zone_init_done)
{
    if (!zone_init_done) {
        /* 首次确定: 无迟滞, 直接用进入阈值 */
        if (dist_m <= TH_IN_CAR__ENTER)           return PEPS_ZONE_IN_CAR;
        if (dist_m <= TH_UNLOCK_EXIT_TO_LOCK)    return PEPS_ZONE_OUTSIDE_UNLOCK;
        if (dist_m <= TH_LOCK_EXIT_TO_INVALID)   return PEPS_ZONE_OUTSIDE_LOCK;
        return PEPS_ZONE_OUTSIDE_INVALID;
    }

    switch (current_zone) {

    case PEPS_ZONE_IN_CAR:
        if (dist_m > TH_IN_CAR__EXIT)     return PEPS_ZONE_OUTSIDE_UNLOCK;
        return PEPS_ZONE_IN_CAR;

    case PEPS_ZONE_OUTSIDE_UNLOCK:
        if (dist_m > TH_UNLOCK_EXIT_TO_LOCK)      return PEPS_ZONE_OUTSIDE_LOCK;
        if (dist_m <= TH_IN_CAR__ENTER)            return PEPS_ZONE_IN_CAR;
        return PEPS_ZONE_OUTSIDE_UNLOCK;

    case PEPS_ZONE_OUTSIDE_LOCK:
        if (dist_m > TH_LOCK_EXIT_TO_INVALID)     return PEPS_ZONE_OUTSIDE_INVALID;
        if (dist_m <= TH_UNLOCK_ENTER_FROM_LOCK)   return PEPS_ZONE_OUTSIDE_UNLOCK;
        return PEPS_ZONE_OUTSIDE_LOCK;

    case PEPS_ZONE_OUTSIDE_INVALID:
        if (dist_m <= TH_LOCK_ENTER_FROM_INVALID)  return PEPS_ZONE_OUTSIDE_LOCK;
        return PEPS_ZONE_OUTSIDE_INVALID;

    default: /* UNKNOWN */
        if (dist_m <= TH_IN_CAR__ENTER)            return PEPS_ZONE_IN_CAR;
        if (dist_m <= TH_UNLOCK_EXIT_TO_LOCK)     return PEPS_ZONE_OUTSIDE_UNLOCK;
        if (dist_m <= TH_LOCK_EXIT_TO_INVALID)    return PEPS_ZONE_OUTSIDE_LOCK;
        return PEPS_ZONE_OUTSIDE_INVALID;
    }
}


/* ========== 内部: 自动解闭锁命令检测 ====================== */
static uint8_t peps_detect_auto_cmd(uint8_t prev_zone, uint8_t new_zone)
{
    if (prev_zone == PEPS_ZONE_OUTSIDE_LOCK
        && new_zone == PEPS_ZONE_OUTSIDE_UNLOCK) {
        return PEPS_AUTO_CMD_UNLOCK;
    }
    if (prev_zone == PEPS_ZONE_OUTSIDE_UNLOCK
        && new_zone == PEPS_ZONE_OUTSIDE_LOCK) {
        return PEPS_AUTO_CMD_LOCK;
    }
    return PEPS_AUTO_CMD_NONE;
}


/* ========== 内部: 主钥匙选择 ============================== */
static void peps_select_primary(void)
{
    float best_dist = 1e9f;
    int   best_idx  = -1;

    for (int i = 0; i < USER_APP_KEY_PEPS_MAX_KEYS; i++) {
        if (g_keys[i].active && g_keys[i].fused_valid) {
            if (g_keys[i].ema_distance_m < best_dist) {
                best_dist = g_keys[i].ema_distance_m;
                best_idx  = i;
            }
        }
    }

    g_primary_idx = best_idx;
}


/* ========== 内部: 处理单把钥匙 ============================ */
static void peps_process_key(key_peps_state_t *key)
{
    uint8_t conn = app_key_conn_handle;
    bool connected = (conn != SL_BT_INVALID_CONNECTION_HANDLE);

    if (!connected || !key->active) {
        if (key->fused_valid) {
            key->fused_valid    = false;
            key->ema_init_done  = false;
            key->current_zone   = PEPS_ZONE_UNKNOWN;
            key->debounce_cnt   = 0;
            key->zone_init_done = false;
        }
        return;
    }

    /* 1. 融合测距 */
    peps_fuse_distance(key);

    if (!key->fused_valid) {
        return;
    }

    /* 2. Zone 判断 (带迟滞) */
    uint8_t raw_zone = peps_determine_zone(key->current_zone,
                                            key->ema_distance_m,
                                            key->zone_init_done);

    /* 3. 防抖: 候选 zone 需连续确认 N 次 */
    if (raw_zone == key->pending_zone) {
        key->debounce_cnt++;
    } else {
        key->pending_zone = raw_zone;
        key->debounce_cnt = 1;
    }

    if (key->debounce_cnt >= ZONE_DEBOUNCE_COUNT) {
        if (key->pending_zone != key->current_zone) {

            USER_LOG_INFO("[PEPS] zone %d -> %d (dist=%.2fm)" USER_LOG_NL,
                          key->current_zone, key->pending_zone,
                          (double)key->ema_distance_m);

            /* 4. 自动解闭锁检测 (冷却期内跳过) */
            uint64_t now_ms = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count64());
            if ((uint32_t)now_ms >= g_cmd_cooldown_ms) {
                uint8_t cmd = peps_detect_auto_cmd(key->current_zone,
                                                    key->pending_zone);
                if (cmd != PEPS_AUTO_CMD_NONE) {
                    g_auto_cmd_pending = cmd;
                    g_cmd_cooldown_ms  = (uint32_t)now_ms + AUTO_CMD_COOLDOWN_MS;

                    USER_LOG_INFO("[PEPS] AUTO %s (cooldown %ums)" USER_LOG_NL,
                                  cmd == PEPS_AUTO_CMD_UNLOCK ? "UNLOCK" : "LOCK",
                                  AUTO_CMD_COOLDOWN_MS);
                }
            }

            key->current_zone = key->pending_zone;
        }
        if (!key->zone_init_done) {
            key->zone_init_done = true;
            USER_LOG_INFO("[PEPS] first zone=%d (dist=%.2fm)" USER_LOG_NL,
                          key->current_zone, (double)key->ema_distance_m);
        }
    }
}


/* ========== Public API =================================== */

void user_app_key_peps_init(void)
{
    memset(g_keys, 0, sizeof(g_keys));
    g_active_count     = 0;
    g_primary_idx      = -1;
    g_auto_cmd_pending = PEPS_AUTO_CMD_NONE;
    g_cmd_cooldown_ms  = 0;

    /* 当前仅 1 把钥匙, 预分配槽位 0 */
    g_keys[0].active       = true;
    g_keys[0].current_zone = PEPS_ZONE_UNKNOWN;
    g_active_count         = 1;

    USER_LOG_INFO("[PEPS] init done (max_keys=%d)" USER_LOG_NL,
                  USER_APP_KEY_PEPS_MAX_KEYS);
}

void user_app_key_peps_process(void)
{
    /* 更新钥匙槽位活跃状态 */
    bool connected = key_connect_is_connected();
    g_keys[0].active = connected;

    if (!connected) {
        g_keys[0].fused_valid    = false;
        g_keys[0].ema_init_done  = false;
        g_keys[0].current_zone   = PEPS_ZONE_UNKNOWN;
        g_keys[0].debounce_cnt   = 0;
        g_keys[0].zone_init_done = false;
        g_primary_idx            = -1;
        return;
    }

    /* 处理每把钥匙 */
    for (int i = 0; i < USER_APP_KEY_PEPS_MAX_KEYS; i++) {
        if (g_keys[i].active) {
            peps_process_key(&g_keys[i]);
        }
    }

    /* 选主钥匙 */
    peps_select_primary();
}

uint8_t user_app_key_peps_get_zone(void)
{
    if (g_primary_idx < 0) {
        return (uint8_t)APP_PROTO_ZONE_DISCONNECTED_UNKNOWN;
    }
    return zone_to_appproto(g_keys[g_primary_idx].current_zone);
}

uint8_t user_app_key_peps_get_auto_cmd(void)
{
    uint8_t cmd = g_auto_cmd_pending;
    g_auto_cmd_pending = PEPS_AUTO_CMD_NONE;
    return cmd;
}

float user_app_key_peps_get_distance(void)
{
    if (g_primary_idx < 0) return 0.0f;
    return g_keys[g_primary_idx].ema_distance_m;
}

bool user_app_key_peps_is_valid(void)
{
    if (g_primary_idx < 0) return false;
    return g_keys[g_primary_idx].fused_valid;
}

void user_app_key_peps_on_disconnected(void)
{
    for (int i = 0; i < USER_APP_KEY_PEPS_MAX_KEYS; i++) {
        g_keys[i].fused_valid    = false;
        g_keys[i].ema_init_done  = false;
        g_keys[i].rtt_has_data   = false;
        g_keys[i].rssi_valid     = false;
        g_keys[i].current_zone   = PEPS_ZONE_UNKNOWN;
        g_keys[i].debounce_cnt   = 0;
        g_keys[i].zone_init_done = false;
    }
    g_primary_idx = -1;

    USER_LOG_DEBUG("[PEPS] disconnected" USER_LOG_NL);
}
