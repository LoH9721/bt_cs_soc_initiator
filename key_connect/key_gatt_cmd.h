/***************************************************************************//**
 * @file key_gatt_cmd.h
 * @brief 控制器侧钥匙通信协议模块
 *
 * 实现 Phase A/B/C 状态机、超时重试、NVM 管理、错误处理。
 * 通过 getter 查询外部状态（连接/bond/GATT 就绪），不依赖回调驱动。
 * app.c 只调用本模块的对外 API，不包含协议逻辑。
 ******************************************************************************/
#ifndef KEY_GATT_CMD_H
#define KEY_GATT_CMD_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_bt_api.h"

// ===== Opcode 定义 =====

typedef enum {
  PROTO_CMD_PUBKEY_REQ      = 0x10,  // 公钥交换请求
  PROTO_CMD_PUBKEY_RSP      = 0x11,  // 公钥交换应答
  PROTO_CMD_SESSION_INIT    = 0x20,  // 安全通道建立请求
  PROTO_CMD_SESSION_RSP     = 0x21,  // 安全通道建立应答
  PROTO_CMD_SESSION_CONFIRM_C = 0x22, // 控制器确认 (AES-CCM)
  PROTO_CMD_SESSION_CONFIRM_K = 0x23, // 钥匙确认 (AES-CCM)
  PROTO_CMD_BUTTON_EVENT    = 0x30,  // 按键事件
  PROTO_CMD_STATUS_REPORT   = 0x31,  // 状态上报
  PROTO_CMD_ACK             = 0x32,  // ACK 应答
  PROTO_CMD_ERROR           = 0xFF,  // 错误帧
} proto_cmd_t;

// ===== 协议阶段 =====

typedef enum {
  PROTO_PHASE_IDLE,
  PROTO_PHASE_A,            // 发送 0x10，等待 0x11
  PROTO_PHASE_B_INIT,       // 发送 0x20，等待 0x21
  PROTO_PHASE_B_CONFIRM,    // 发送 0x22，等待 0x23
  PROTO_PHASE_C,            // 命令交互
} proto_phase_t;

// ===== 错误码 =====

typedef enum {
  PROTO_ERR_SIG_FAIL     = 0x01,  // 签名验证失败
  PROTO_ERR_SESSION_FAIL = 0x02,  // 会话密钥确认失败
  PROTO_ERR_DECRYPT_FAIL = 0x03,  // 解密失败 (CCM MIC 不匹配)
  PROTO_ERR_TIMEOUT      = 0x04,  // 超时无响应
  PROTO_ERR_SEQ_FAULT    = 0x05,  // 序列号异常
  PROTO_ERR_PHASEA_REJECT = 0x06, // Phase A 拒绝 (NVM 不对称)
  PROTO_ERR_VER_MISMATCH  = 0x07, // 协议版本不兼容
  PROTO_ERR_NEED_PHASEA   = 0x08, // 需先 Phase A (NVM 不对称)
} proto_error_t;

// ===== Phase C 业务回调 =====

// 收到 0x30 按键事件后的回调
typedef void (*proto_button_cb_t)(uint8_t btn1, uint8_t btn2, uint8_t btn3,
                                   uint16_t battery_mv, uint8_t fault_flags);

// 收到 0x31 状态上报后的回调
typedef void (*proto_status_cb_t)(uint16_t battery_mv, int8_t rssi, uint8_t fault_flags);

// ===== 对外 API (由 app.c 调用) =====

// 初始化协议模块 (注册 RX 回调，清零状态)
void key_gatt_proto_init(void);

// 主循环驱动 (轮询超时、推进状态机)
void key_gatt_proto_process_action(void);

// BLE 事件分发 (仅转发连接关闭等事件)
void key_gatt_proto_on_bt_event(sl_bt_msg_t *evt);

// 查询是否已进入 Phase C (加密通信就绪)
bool key_gatt_proto_is_in_phase_c(void);

// 清除协议 NVM (配对/恢复出厂时调用，确保下次走 Phase A)
void key_gatt_proto_reset_pairing(void);

// 注册 Phase C 业务回调
void key_gatt_proto_set_button_callback(proto_button_cb_t cb);
void key_gatt_proto_set_status_callback(proto_status_cb_t cb);

#endif /* KEY_GATT_CMD_H */
