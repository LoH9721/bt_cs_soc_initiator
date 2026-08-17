/***************************************************************************//**
 * @file hid_service.h
 * @brief HID 承载模块 —— 无感蓝牙钥匙 (CR-008 PASSIVE) 的系统级 Bond/后台回连承载。
 *
 * 与原始独立 HID 外设方案不同, 本模块已统一到手机 APP 外设 (BLEKEY_XXXX):
 *   - 不拥有身份地址 / SM / 绑定库 / 独立广播集 (这些由 phone_link / phone_sm 统一管理)
 *   - 只负责「根据无感运行时状态, 在现有手机广播里加入/移除 HID 特征
 *     (HID UUID 0x1812 + Battery 0x180F + Appearance 0x08C1 (Car))」+ 状态查询
 *   - HID 服务本身已静态写入 GATT 数据库 (见 gatt_configuration.btconf), 运行时不增删
 *   - 名称复用手机广播名 BLEKEY_XXXX (不另行命名)
 *   - 配对 PIN = pairingPasskey, 由 phone_sm 的 PASSIVE_PAIR_PREPARE/READY 生成与下发,
 *     本模块不参与 PIN 的生成与设置
 *
 * 无论 HID 运行时开关如何, 均不影响自定义 APP GATT (phone_rx/phone_tx) 通信。
 ******************************************************************************/
#ifndef HID_SERVICE_H
#define HID_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "sl_bt_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 设置 HID 运行时开关 (无感钥匙启用/关闭的承载层开关)。
 *
 * - on=true:  手机广播加入 HID 特征 (HID UUID + Battery UUID + Appearance 0x08C1 (Car)),
 *             供手机 OS 识别为 HID 设备并后台自动回连
 * - on=false: 广播移除 HID 特征, 保持纯 BLEKEY_XXXX (APP 手动连接不受影响)
 *
 * 运行态即时生效, 无需复位; 广播数据在下一次构建时按此开关生成。
 */
void hid_service_set_runtime_enabled(bool on);

/** HID 运行时是否开启 (决定广播是否携带 HID 特征) */
bool hid_service_is_runtime_enabled(void);

/** 当前是否有手机 (Peripheral 角色) 连接 */
bool hid_service_is_connected(void);

/** 绑定表中是否存在 Bond (等价于 PASSIVE_ENABLE 的 Bond 检查依据) */
bool hid_service_is_bonded(void);

/**
 * @brief 构造 HID 广播 AD 块 (Complete 16-bit UUIDs: HID+Battery; Appearance 0x08C1 (Car))。
 *
 * @param out_len 输出: AD 块长度
 * @return 静态缓冲区指针 (out_len>0 时有效); 未开启 HID 时返回 NULL 且 *out_len=0
 */
const uint8_t *hid_service_build_adv_block(uint8_t *out_len);

/**
 * @brief 设置调试固定配对 PIN (覆盖 phone_sm 的 TRNG 随机 Passkey)。
 *
 * @param pin 6 位 PIN (100000~999999); 0 = 清除覆盖(恢复随机 PIN)
 */
void hid_service_set_pin(uint32_t pin);

/** 返回调试固定 PIN (0 = 无覆盖, 由 phone_sm 用 TRNG 随机) */
uint32_t hid_service_get_pin(void);

/**
 * @brief 蓝牙事件入口 (由 app.c 在 Peripheral 分支分发)。
 *
 * 仅跟踪连接状态用于诊断, 不配置 SM / 身份地址 / 绑定 / 广播。
 */
void hid_service_on_bt_event(sl_bt_msg_t *evt);

#ifdef __cplusplus
}
#endif

#endif // HID_SERVICE_H
