/***************************************************************************//**
 * @file phone_adv.h
 * @brief V1.1 广播数据构造 (第 3 / 22.7 节)
 *
 * Advertising Data:  Flags + Manufacturer Specific
 * Scan Response:     Complete Local Name = "BLEKEY_XXXX"
 *
 * shortDid = SHA256(deviceId) 前 2 Byte
 * deviceState: 0x01=UNBOUND, 0x02=BOUND, 0x03=REBIND_WINDOW,
 *              0x04=SILENT, 0x05=SECURITY_LOCKED, 0x06=SERVICE_MODE, 0x07=ERROR
 ******************************************************************************/
#ifndef PHONE_ADV_H
#define PHONE_ADV_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** 广播数据总长度: AD Flags(3) + AD Manufacturer(2+9) = 14 */
#define PHONE_ADV_DATA_MAX_LEN    31U
#define PHONE_SCAN_RESP_MAX_LEN   32U

/**
 * @brief 构造 Advertising Data (Flags + Manufacturer Specific)
 * @param device_state 当前设备状态 (PHONE_DEVICE_STATE_*)
 * @param out_buf      输出缓冲区
 * @param out_len      输出长度
 */
void phone_adv_build_advertising_data(uint8_t device_state,
                                      uint8_t *out_buf, uint8_t *out_len);

/**
 * @brief 构造 Scan Response Data (Complete Local Name)
 * @param out_buf  输出缓冲区
 * @param out_len  输出长度
 */
void phone_adv_build_scan_response(uint8_t *out_buf, uint8_t *out_len);

/**
 * @brief 获取当前广播名称 (BLEKEY_XXXX)
 * @param name_buf 输出缓冲区 (至少 16 Byte)
 */
void phone_adv_get_name(char *name_buf, uint8_t buf_size);

/**
 * @brief 根据设备状态获取广播间隔 (单位 0.625ms)
 */
uint16_t phone_adv_get_interval(uint8_t device_state);

/**
 * @brief 根据设备状态获取 capabilityFlags (广播中最后 1 字节)
 *
 * 不同状态下广播的能力位不同, 例如 SILENT 状态下不应广播控制能力。
 * 后续根据实际需求调整各状态的返回值。
 *
 * @param device_state 当前设备状态 (PHONE_DEVICE_STATE_*)
 * @return capabilityFlags (低 8 位有效, 高 24 位预留)
 */
uint32_t phone_adv_get_capability_flags(uint8_t device_state);

/**
 * @brief 计算 shortDid = SHA256(deviceId) 前 2 Byte
 * @param short_did 输出 (2 Byte, Big Endian)
 */
void phone_adv_calc_short_did(uint8_t short_did[2]);

#ifdef __cplusplus
}
#endif

#endif /* PHONE_ADV_H */
