/***************************************************************************//**
 * @file key_gatt_rang.c
 * @brief 钥匙 RSSI 距离估算模块实现
 *
 * 对数路径损耗模型: distance = 10 ^ ((tx_power_1m - rssi) / (10 * n))
 *   - tx_power_1m: 1m 处参考 RSSI (dBm)
 *   - n: 路径损耗指数 (自由空间 2.0, 室内 2.5~3.5, 遮挡 4.0+)
 *
 * 本端 RSSI 通过 sl_bt_connection_get_median_rssi 周期获取 (连接态)。
 * 远端 RSSI 由 proto_status_cb_t 回调喂入 (GATT 状态上报)。
 * 无连接时所有值标记无效。
 ******************************************************************************/
#include "key_gatt_rang.h"
#include "app.h"
#include "sl_bt_api.h"
#include "sl_sleeptimer.h"
#include "user_log_console.h"
#include <math.h>
#include <string.h>

/* ========== 默认配置 ========== */
#define DEFAULT_TX_POWER_1M        (-59)
#define DEFAULT_PATH_LOSS_N        2.0f
#define DEFAULT_UPDATE_INTERVAL_MS 1000

/* ========== 静态状态 ========== */
static key_rssi_rang_cfg_t    g_cfg;
static key_rssi_rang_result_t g_result;
static bool     g_connected;
static uint64_t g_next_rssi_ms;

/* ========== 内部: RSSI → 距离 ========== */
static float rssi_to_distance_m(int8_t rssi, int8_t tx_power, float n)
{
    float exponent = (float)(tx_power - rssi) / (10.0f * n);
    return powf(10.0f, exponent);
}

/* ========== Public API ========== */

void key_rssi_rang_init(void)
{
    g_cfg.tx_power_1m        = DEFAULT_TX_POWER_1M;
    g_cfg.path_loss_n        = DEFAULT_PATH_LOSS_N;
    g_cfg.update_interval_ms = DEFAULT_UPDATE_INTERVAL_MS;

    memset(&g_result, 0, sizeof(g_result));
    g_result.local_valid  = false;
    g_result.remote_valid = false;

    g_connected    = false;
    g_next_rssi_ms = 0;

    USER_LOG_DEBUG("[RSSI_RANG] init: tx_1m=%d n=%.1f interval=%ums" USER_LOG_NL,
                   g_cfg.tx_power_1m, (double)g_cfg.path_loss_n,
                   g_cfg.update_interval_ms);
}

void key_rssi_rang_configure(const key_rssi_rang_cfg_t *cfg)
{
    if (cfg == NULL) return;
    g_cfg = *cfg;
    USER_LOG_DEBUG("[RSSI_RANG] cfg: tx_1m=%d n=%.1f interval=%ums" USER_LOG_NL,
                   g_cfg.tx_power_1m, (double)g_cfg.path_loss_n,
                   g_cfg.update_interval_ms);
}

void key_rssi_rang_process(void)
{
    uint64_t now = sl_sleeptimer_tick_to_ms(sl_sleeptimer_get_tick_count64());
    uint8_t conn = app_key_conn_handle;
    bool connected = (conn != SL_BT_INVALID_CONNECTION_HANDLE);

    if (!connected) {
        if (g_connected) {
            key_rssi_rang_on_disconnected();
        }
        return;
    }

    if (!g_connected) {
        g_connected    = true;
        g_next_rssi_ms = 0; /* 立即请求 */
    }

    /* 周期获取本端 RSSI */
    if (now >= g_next_rssi_ms) {
        g_next_rssi_ms = now + g_cfg.update_interval_ms;

        int8_t rssi = 0;
        sl_status_t sc = sl_bt_connection_get_median_rssi(conn, &rssi);
        if (sc == SL_STATUS_OK) {
            key_rssi_rang_feed_local_rssi(rssi);
        }
    }
}

void key_rssi_rang_feed_local_rssi(int8_t rssi)
{
    g_result.rssi_local  = rssi;
    g_result.local_valid = g_connected;
    if (g_connected) {
        g_result.dist_local_m = rssi_to_distance_m(rssi,
            g_cfg.tx_power_1m, g_cfg.path_loss_n);
    }
}

void key_rssi_rang_feed_remote_rssi(int8_t rssi)
{
    g_result.rssi_remote  = rssi;
    g_result.remote_valid = g_connected;
    if (g_connected) {
        g_result.dist_remote_m = rssi_to_distance_m(rssi,
            g_cfg.tx_power_1m, g_cfg.path_loss_n);
    }
}

void key_rssi_rang_on_disconnected(void)
{
    g_connected    = false;
    g_result.local_valid  = false;
    g_result.remote_valid = false;
    g_result.rssi_local   = 0;
    g_result.rssi_remote  = 0;
    g_result.dist_local_m  = 0.0f;
    g_result.dist_remote_m = 0.0f;

    USER_LOG_DEBUG("[RSSI_RANG] disconnected, all invalid" USER_LOG_NL);
}

bool key_rssi_rang_get_result(key_rssi_rang_result_t *out)
{
    if (out == NULL) return false;
    *out = g_result;
    return (g_result.local_valid || g_result.remote_valid);
}

bool key_rssi_rang_is_local_valid(void)  { return g_result.local_valid; }
bool key_rssi_rang_is_remote_valid(void) { return g_result.remote_valid; }
