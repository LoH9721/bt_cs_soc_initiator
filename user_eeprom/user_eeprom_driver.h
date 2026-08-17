/***************************************************************************//**
 * @file user_eeprom_driver.h
 * @brief 存储驱动抽象接口。
 *
 * 替换存储介质时 (NVM3 → I2C EEPROM → SPI Flash):
 *   1. 新建 xxx_driver.c, 实现 user_eeprom_driver_t 的 5 个函数指针
 *   2. 在 user_eeprom_init() 前调用 user_eeprom_driver_register(&xxx_driver)
 *   3. user_eeprom 核心逻辑 (CRC / RAM缓存 / 异步队列) 完全不变
 ******************************************************************************/
#ifndef USER_EEPROM_DRIVER_H
#define USER_EEPROM_DRIVER_H

#include <stdint.h>
#include "sl_status.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  /**
   * @brief 驱动初始化 (如 I2C 总线初始化, NULL=不需要)
   */
  sl_status_t (*init)(void);

  /**
   * @brief 读取数据
   * @param addr  逻辑地址 (NVM3: object key; I2C: byte offset)
   * @param buf   输出缓冲区
   * @param len   读取长度
   * @note  仅在 init() 预加载阶段被批量调用;
   *         正常运行中 read() 只读 RAM 缓存, 不会调用此函数
   */
  sl_status_t (*read)(uint32_t addr, void *buf, uint16_t len);

  /**
   * @brief 写入数据
   * @note  在 user_eeprom_process() 上下文中调用 (非中断, 允许阻塞 5~10ms)
   */
  sl_status_t (*write)(uint32_t addr, const void *buf, uint16_t len);

  /**
   * @brief 删除单个条目
   */
  sl_status_t (*erase)(uint32_t addr);

  /**
   * @brief 全片擦除 (可选, 工厂复位用, NULL=逐条擦除)
   */
  sl_status_t (*erase_all)(void);

  /**
   * @brief 单次写入最大耗时 (ms), 供分片调度参考
   *        NVM3 Flash page: ~5ms;  I2C EEPROM page: ~10ms
   *        返回 0 表示不限制 (驱动自行保证不长时间阻塞)
   */
  uint16_t (*max_write_time_ms)(void);

  /** 驱动名称 (调试用) */
  const char *name;
} user_eeprom_driver_t;

/** 注册存储驱动 */
void user_eeprom_driver_register(const user_eeprom_driver_t *driver);

#ifdef __cplusplus
}
#endif

#endif /* USER_EEPROM_DRIVER_H */
