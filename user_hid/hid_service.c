/***************************************************************************//**
 * @file hid_service.c
 * @brief HID 承载模块 —— 无感蓝牙钥匙的系统级 Bond/后台回连承载。
 *
 * 统一到手机 APP 外设 (BLEKEY_XXXX): 不拥有身份地址/SM/绑定/独立广播,
 * 只按 hid_runtime_enabled 在手机广播里加入/移除 HID 特征, 并提供状态查询。
 ******************************************************************************/

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "sl_bt_api.h"
#include "user_hid/hid_service.h"
#include "user_log_console.h"

/* ========================================================================== */
/* 内部状态                                                                     */
/* ========================================================================== */

static bool    hid_runtime_enabled = false;  /* 无感运行时开关 (决定广播是否带 HID 特征) */
static uint8_t hid_conn_handle     = SL_BT_INVALID_CONNECTION_HANDLE; /* 当前手机连接句柄 */
static bool    hid_connected       = false;

/* 调试覆盖 (由串口 hid_pin 设置, 0 = 无覆盖) */
static uint32_t hid_debug_pin       = 0U;

/* ========================================================================== */
/* HID 广播 AD 块 (静态, 内容固定)                                              */
/*   Complete List of 16-bit Service UUIDs: HID(0x1812)                        */
/*   (Battery UUID 0x180F 已移到 scan response, 见 phone_adv.c)                */
/*   Appearance: 0x08C1 = Car (little-endian: C1 08)                          */
/* ========================================================================== */

static const uint8_t hid_adv_block[] = {
  0x03U, 0x03U, 0x12U, 0x18U,                  /* HID UUID (0x1812) */
  0x03U, 0x19U, 0xC1U, 0x08U                   /* Appearance 0x08C1 Car */
};
static const uint8_t hid_adv_block_len = (uint8_t)sizeof(hid_adv_block);

/* ========================================================================== */
/* 公共 API                                                                    */
/* ========================================================================== */

void hid_service_set_runtime_enabled(bool on)
{
  if (hid_runtime_enabled == on) {
    return;
  }
  hid_runtime_enabled = on;
  USER_LOG_INFO("[HID] runtime := %s (广播%sHID特征)" USER_LOG_NL,
                on ? "ON" : "OFF", on ? "加入" : "移除");
}

bool hid_service_is_runtime_enabled(void)
{
  return hid_runtime_enabled;
}

bool hid_service_is_connected(void)
{
  return hid_connected;
}

bool hid_service_is_bonded(void)
{
  uint8_t  mask[4] = {0, 0, 0, 0};  /* SDK: 4 byte bit field (bonding handle 0-31) */
  uint32_t num_bondings = 0;
  size_t   bondings_len = 0;

  sl_status_t sc = sl_bt_sm_get_bonding_handles(0, &num_bondings,
                                                sizeof(mask), &bondings_len, mask);
  return (sc == SL_STATUS_OK && num_bondings > 0U);
}

const uint8_t *hid_service_build_adv_block(uint8_t *out_len)
{
  if (out_len == NULL) {
    return NULL;
  }
  if (!hid_runtime_enabled) {
    *out_len = 0;
    return NULL;
  }
  *out_len = hid_adv_block_len;
  return hid_adv_block;
}

void hid_service_set_pin(uint32_t pin)
{
  hid_debug_pin = pin;
  USER_LOG_INFO("[HID] set_pin=%lu (%s)" USER_LOG_NL,
                (unsigned long)pin,
                (pin >= 100000UL && pin <= 999999UL) ? "固定PIN覆盖" : "清除覆盖(随机PIN)");
}

uint32_t hid_service_get_pin(void)
{
  return hid_debug_pin;
}

/* ========================================================================== */
/* 蓝牙事件处理 (仅跟踪连接状态)                                                 */
/* ========================================================================== */

void hid_service_on_bt_event(sl_bt_msg_t *evt)
{
  if (evt == NULL) {
    return;
  }

  switch (SL_BT_MSG_ID(evt->header)) {
    case sl_bt_evt_connection_opened_id: {
      const sl_bt_evt_connection_opened_t *d = &evt->data.evt_connection_opened;
      if (d->role != sl_bt_connection_role_peripheral) {
        break;
      }
      hid_conn_handle = d->connection;
      hid_connected   = true;
      USER_LOG_INFO("[HID] connected conn=%u" USER_LOG_NL, (unsigned)d->connection);
      break;
    }

    case sl_bt_evt_connection_closed_id: {
      const sl_bt_evt_connection_closed_t *d = &evt->data.evt_connection_closed;
      if (d->connection != hid_conn_handle) {
        break;
      }
      USER_LOG_INFO("[HID] disconnected conn=%u" USER_LOG_NL, (unsigned)d->connection);
      hid_conn_handle = SL_BT_INVALID_CONNECTION_HANDLE;
      hid_connected   = false;
      break;
    }

    default:
      break;
  }
}
