/***************************************************************************//**
 * @file key_gatt_rang.h
 * @brief 钥匙 RSSI 距离估算模块
 *
 * 使用对数路径损耗模型将 RSSI 转换为估算距离。
 * RSSI 来源:
 *   - 本端 RSSI: 控制器端收到钥匙信号的 RSSI (扫描/连接)
 *   - 远端 RSSI: 钥匙端收到控制器信号的 RSSI (GATT 状态上报)
 *
 * 无连接时所有值标记为不可用。
 ******************************************************************************/
#ifndef KEY_GATT_RANG_H
#define KEY_GATT_RANG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** RSSI 距离估算配置 */
typedef struct {
    int8_t  tx_power_1m;    /* 1 米处参考 RSSI (dBm), 默认 -59 */
    float   path_loss_n;    /* 路径损耗指数, 默认 2.0 (自由空间 2.0, 室内 2.5~3.5) */
    uint16_t update_interval_ms;  /* 本端 RSSI 轮询间隔 (ms), 默认 1000 */
} key_rssi_rang_cfg_t;

/** RSSI 距离估算结果 */
typedef struct {
    float   dist_local_m;   /* 本端 RSSI 估算距离 (m) */
    float   dist_remote_m;  /* 远端 RSSI 估算距离 (m) */
    int8_t  rssi_local;     /* 最近一次本端 RSSI (dBm) */
    int8_t  rssi_remote;    /* 最近一次远端 RSSI (dBm) */
    bool    local_valid;    /* 本端数据是否有效 */
    bool    remote_valid;   /* 远端数据是否有效 */
} key_rssi_rang_result_t;


/** 初始化配置为默认值 */
void key_rssi_rang_init(void);

/** 修改配置 (仅可在未连接时调用) */
void key_rssi_rang_configure(const key_rssi_rang_cfg_t *cfg);

/** 主循环: 周期请求本端 RSSI, 更新估算 */
void key_rssi_rang_process(void);

/** 喂入本端 RSSI (由 BLE 事件/扫描报告调用) */
void key_rssi_rang_feed_local_rssi(int8_t rssi);

/** 喂入远端 RSSI (由 proto_status_cb_t 回调调用) */
void key_rssi_rang_feed_remote_rssi(int8_t rssi);

/** 连接断开时调用, 标记所有数据无效 */
void key_rssi_rang_on_disconnected(void);

/** 获取当前估算结果 */
bool key_rssi_rang_get_result(key_rssi_rang_result_t *out);

/** 查询本端数据有效性 */
bool key_rssi_rang_is_local_valid(void);

/** 查询远端数据有效性 */
bool key_rssi_rang_is_remote_valid(void);

#ifdef __cplusplus
}
#endif

#endif /* KEY_GATT_RANG_H */
