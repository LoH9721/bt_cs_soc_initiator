/***************************************************************************//**
 * @file user_eeprom_driver_nvm3.c
 * @brief NVM3 存储驱动 (默认驱动, 基于 Silicon Labs NVM3)
 ******************************************************************************/

#include "user_eeprom_driver.h"
#include "nvm3_default.h"

/* =================================================================== */
/* NVM3 驱动实现                                                        */
/* =================================================================== */

static sl_status_t nvm3_read(uint32_t addr, void *buf, uint16_t len)
{
  return nvm3_readData(nvm3_defaultHandle, addr, buf, (size_t)len);
}

static sl_status_t nvm3_write(uint32_t addr, const void *buf, uint16_t len)
{
  sl_status_t sc = nvm3_writeData(nvm3_defaultHandle, addr, buf, (size_t)len);
  if (sc == SL_STATUS_OK) return SL_STATUS_OK;

  /* Flash 满: repack 后重试一次 */
  if (sc == (sl_status_t)ECODE_NVM3_ERR_STORAGE_FULL) {
    (void)nvm3_repack(nvm3_defaultHandle);
    sc = nvm3_writeData(nvm3_defaultHandle, addr, buf, (size_t)len);
  }
  return sc;
}

static sl_status_t nvm3_erase(uint32_t addr)
{
  sl_status_t sc = nvm3_deleteObject(nvm3_defaultHandle, addr);
  /* 删除不存在的对象不算失败 */
  if (sc == (sl_status_t)ECODE_NVM3_ERR_KEY_NOT_FOUND) {
    return SL_STATUS_OK;
  }
  return sc;
}

static uint16_t nvm3_max_write_ms(void)
{
  /* NVM3 单次 Flash page write 约 3~5ms */
  return 5U;
}

/* =================================================================== */
/* 驱动实例                                                             */
/* =================================================================== */

const user_eeprom_driver_t g_eeprom_driver_nvm3 = {
  .init             = NULL,               /* NVM3 由 sl_platform_init 初始化 */
  .read             = nvm3_read,
  .write            = nvm3_write,
  .erase            = nvm3_erase,
  .erase_all        = NULL,               /* 不使用全片擦除 */
  .max_write_time_ms = nvm3_max_write_ms,
  .name             = "NVM3",
};
