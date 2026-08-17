/***************************************************************************//**
 * @file phone_adv.c
 * @brief V1.1 广播数据构造实现。
 ******************************************************************************/

#include "user_phone/data/phone_adv.h"
#include "user_phone/data/phone_crypto.h"
#include "user_phone/data/phone_storage.h"
#include "user_phone/phone_cfg.h"
#include "user_hid/hid_service.h"
#include <string.h>
#include <stdio.h>

/* ========================================================================== */
/* shortDid 计算                                                               */
/* ========================================================================== */

void phone_adv_calc_short_did(uint8_t short_did[2])
{
  char did[PHONE_DEVICE_ID_MAX_LEN + 1];
  uint8_t hash[32];

  memset(did, 0, sizeof(did));
  /* 上电时已从芯片 UID 派生, 必然有效 */
  (void)phone_storage_get_device_id(did, sizeof(did));
  (void)phone_crypto_sha256((const uint8_t *)did, (uint16_t)strlen(did), hash);
  short_did[0] = hash[0];
  short_did[1] = hash[1];
}

/* ========================================================================== */
/* 广播名称                                                                    */
/* ========================================================================== */

void phone_adv_get_name(char *name_buf, uint8_t buf_size)
{
  uint8_t short_did[2];

  if (name_buf == NULL || buf_size < 14U) return;

  phone_adv_calc_short_did(short_did);

  /* BLEKEY_XXXX (XXXX = shortDid hex) */
  snprintf(name_buf, buf_size, "%s%02X%02X",
           PHONE_ADV_NAME_PREFIX,
           (unsigned)short_did[0],
           (unsigned)short_did[1]);
}

/* ========================================================================== */
/* capabilityFlags 映射 — 根据设备状态返回广播中宣告的能力位                    */
/* ========================================================================== */

uint32_t phone_adv_get_capability_flags(uint8_t device_state)
{
  switch (device_state) {
    case PHONE_DEVICE_STATE_UNBOUND:
      /* 未绑定时仅宣告基础安全能力, 不宣告控制/换绑 */
      return PHONE_CAP_AES_CCM_SESSION;

    case PHONE_DEVICE_STATE_BOUND:
      /* 已绑定: 全部 V1.1 能力 */
      return PHONE_CAP_DEFAULT_V11;

    case PHONE_DEVICE_STATE_REBIND_WINDOW:
      /* 换绑窗口: 同已绑定, 允许换绑 */
      return PHONE_CAP_DEFAULT_V11;

    case PHONE_DEVICE_STATE_SILENT:
      /* 静默期: 仅基础安全能力, 不宣告控制/换绑 */
      return PHONE_CAP_AES_CCM_SESSION;

    case PHONE_DEVICE_STATE_SECURITY_LOCKED:
      /* 安全锁定: 仅基础安全能力 */
      return PHONE_CAP_AES_CCM_SESSION;

    case PHONE_DEVICE_STATE_SERVICE_MODE:
      /* 服务模式: 全部能力 */
      return PHONE_CAP_DEFAULT_V11;

    case PHONE_DEVICE_STATE_ERROR:
      /* 异常: 仅宣告自身存在 */
      return 0U;

    case PHONE_DEVICE_STATE_UNKNOWN:
    default:
      /* 未知: 仅宣告自身存在 */
      return 0U;
  }
}

/* ========================================================================== */
/* Advertising Data                                                           */
/* ========================================================================== */

void phone_adv_build_advertising_data(uint8_t device_state,
                                      uint8_t *out_buf, uint8_t *out_len)
{
  uint8_t short_did[2];
  uint8_t offset = 0U;
  uint32_t caps = phone_adv_get_capability_flags(device_state);

  if (out_buf == NULL || out_len == NULL) return;

  phone_adv_calc_short_did(short_did);

  /* AD Flags: 0x02 | 0x01 | 0x06 */
  out_buf[offset++] = 0x02U;   /* Length */
  out_buf[offset++] = 0x01U;   /* AD Type: Flags */
  out_buf[offset++] = 0x06U;   /* LE General Discoverable + BR/EDR Not Supported */

  /* AD Manufacturer Specific: 0x0A | 0xFF | CompanyId(LE) | B1 24 | advVersion | shortDid(2) | deviceState(1) | capabilityFlags(1) */
  out_buf[offset++] = 0x0AU;    /* Length = 10 */
  out_buf[offset++] = 0xFFU;    /* AD Type: Manufacturer Specific Data */
  out_buf[offset++] = (uint8_t)(PHONE_ADV_COMPANY_ID & 0xFFU);        /* Company ID Low Byte */
  out_buf[offset++] = (uint8_t)((PHONE_ADV_COMPANY_ID >> 8) & 0xFFU); /* Company ID High Byte */
  out_buf[offset++] = PHONE_ADV_MAGIC_B1;
  out_buf[offset++] = PHONE_ADV_MAGIC_B2;
  out_buf[offset++] = PHONE_ADV_VERSION;
  out_buf[offset++] = short_did[0];
  out_buf[offset++] = short_did[1];
  out_buf[offset++] = device_state;
  out_buf[offset++] = (uint8_t)(caps & 0xFFU);  /* capabilityFlags low 8 */

  /* V1.2 无感: HID 运行时开启时追加 HID 特征 (HID UUID + Battery UUID + Appearance 0x08C1 (Car)),
   * 供手机 OS 识别为 HID 设备并后台自动回连; 关闭时保持纯 BLEKEY 广播 (不影响 APP 连接) */
  if (hid_service_is_runtime_enabled()) {
    uint8_t hid_len = 0U;
    const uint8_t *hid_blk = hid_service_build_adv_block(&hid_len);
    if (hid_blk != NULL && hid_len > 0U
        && (offset + hid_len) <= PHONE_ADV_DATA_MAX_LEN) {
      memcpy(&out_buf[offset], hid_blk, hid_len);
      offset += hid_len;
    }
  }

  *out_len = offset;
}

/* ========================================================================== */
/* Scan Response Data                                                          */
/* ========================================================================== */

void phone_adv_build_scan_response(uint8_t *out_buf, uint8_t *out_len)
{
  char name[32];
  uint8_t name_len;
  uint8_t offset = 0U;

  if (out_buf == NULL || out_len == NULL) return;

  phone_adv_get_name(name, sizeof(name));
  name_len = (uint8_t)strlen(name);

  /* AD Complete Local Name: Length | 0x09 | name */
  out_buf[offset++] = name_len + 1U;
  out_buf[offset++] = 0x09U;  /* AD Type: Complete Local Name */
  memcpy(&out_buf[offset], name, name_len);
  offset += name_len;

  /* V1.2: HID 开启时 Battery UUID(0x180F) 放 scan response (HOGP 服务集提示, 不占 ADV 空间) */
  if (hid_service_is_runtime_enabled()) {
    out_buf[offset++] = 0x03U;   /* Length = 3 (type + 1 UUID) */
    out_buf[offset++] = 0x03U;   /* AD Type: Complete List of 16-bit Service UUIDs */
    out_buf[offset++] = 0x0FU;   /* Battery UUID low */
    out_buf[offset++] = 0x18U;   /* Battery UUID high */
  }

  *out_len = offset;
}

/* ========================================================================== */
/* 广播间隔                                                                    */
/* ========================================================================== */

uint16_t phone_adv_get_interval(uint8_t device_state)
{
  switch (device_state) {
    case PHONE_DEVICE_STATE_UNBOUND:
      return PHONE_ADV_INTERVAL_UNBOUND_MIN;
    case PHONE_DEVICE_STATE_SILENT:
      return PHONE_ADV_INTERVAL_SILENT_MIN;
    case PHONE_DEVICE_STATE_BOUND:
    default:
      return PHONE_ADV_INTERVAL_BOUND_MIN;
  }
}
