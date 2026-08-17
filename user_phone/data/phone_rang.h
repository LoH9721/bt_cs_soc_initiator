/***************************************************************************//**
 * @file phone_rang.h
 * @brief 手机 RSSI 距离估算模块
 *
 * 使用对数路径损耗模型将 RSSI 转换为估算距离，1D 卡尔曼滤波平滑。
 * RSSI 来源:
 *   - 本端 RSSI: 设备端收到手机信号的 RSSI (周期轮询)
 *   - 远端 RSSI: 手机端收到设备信号的 RSSI (0x12 帧喂入)
 *
 * 无连接时所有值标记为不可用。
 ******************************************************************************/
#ifndef PHONE_RANG_H
#define PHONE_RANG_H

#include <stdbool.h>
#include <stdint.h>

/* ========== 编译期默认值 (NVM 无效时的回退值) ========== */
#define PHONE_RANG_DEFAULT_RSSI_1M           (-58)   /* 1m 处 RSSI 默认值 */
#define PHONE_RANG_DEFAULT_RSSI_10M          (-85)   /* 10m 处 RSSI 默认值, 对应 n=2.7 */
#define PHONE_RANG_DEFAULT_PATH_LOSS_N       2.70f   /* n 默认值 (安全回退) */

#ifdef __cplusplus
extern "C" {
#endif

void phone_rang_init(void);

/** 主循环: 周期请求本端 RSSI, 更新卡尔曼融合距离 */
void phone_rang_process_action(void);

/** 喂入远端 RSSI (由 phone_cmd 解析 0x12 帧后调用) */
void phone_rang_feed_remote_rssi(int8_t rssi);

/** 重置所有数据 (断连 / 进入 NORMAL 时调用) */
void phone_rang_reset(void);

/** 获取卡尔曼滤波后的融合距离 (m), 无有效数据返回 false */
bool phone_rang_get_distance(float *dist_m);

/** 距离估算是否有效 */
bool phone_rang_is_valid(void);

/** 获取本端 RSSI (ECU 收到手机信号的 RSSI, 单位 dBm), 无有效数据返回 false */
bool phone_rang_get_local_rssi(int8_t *rssi);

/** 获取远端 RSSI (手机上报的 RSSI, 单位 dBm), 无有效数据返回 false */
bool phone_rang_get_remote_rssi(int8_t *rssi);

/* ========== 校准参数管理 ========== */

/** 重新加载校准: 从 NVM 读取 rssi_1m / rssi_10m, 内部反算 n */
void phone_rang_reload_calibration(void);

/** 获取当前生效的校准参数 (rssi_1m / rssi_10m 为 NVM 原始值或默认值, n 为内部反算值) */
void phone_rang_get_calibration(int8_t *rssi_1m, int8_t *rssi_10m, float *path_loss_n);

/** 自动标定: 输入 1m 和 10m 处 RSSI, NVM 存原始值, 内部反算 n = (rssi1m-rssi10m)/10 */
void phone_rang_cal_auto(int8_t rssi_1m, int8_t rssi_10m);

/** 清除 NVM 校准数据, 回退默认值 */
void phone_rang_clear_calibration(void);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_RANG_H */
