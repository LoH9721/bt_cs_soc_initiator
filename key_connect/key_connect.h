#ifndef KEY_CONNECT_H
#define KEY_CONNECT_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_bt_api.h"
#include "ble_peer_manager_common.h"

// 主循环处理 (扫描keepalive / 配对超时 / 延迟SMP)
void key_connect_process_action(void);

// 初始化: SM配置 + 过滤器 + 自动开始NORMAL_SCAN
void key_connect_init(void);

// 正常扫描 (peer manager默认过滤)
void key_connect_start_scan(void);

// 连接到指定地址 "AA:BB:CC:DD:EE:FF"
void key_connect_open(const char *addr_str);

// 断开当前连接
void key_connect_disconnect(void);

// 自动连接白名单设备 (配好filter后开始扫)
void key_connect_auto_connect(void);

// 添加白名单地址
void key_connect_add_whitelist(const char *addr_str);

// 进入配对扫描模式 (只连0xA5广播)
void key_connect_enter_pairing_mode(void);

// BLE原始事件处理
void key_connect_on_bt_event(sl_bt_msg_t *evt);

// Peer Manager事件处理
void key_connect_on_peer_manager_event(ble_peer_manager_evt_type_t *event);

// BLE 连接状态查询 (供协议模块 getter 使用)
bool key_connect_is_connected(void);
bool key_connect_is_bonded(void);

#endif
