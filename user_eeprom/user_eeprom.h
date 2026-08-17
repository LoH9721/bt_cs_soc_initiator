/***************************************************************************//**
 * @file user_eeprom.h
 * @brief 统一 EEPROM 存储管理 —— 公共接口
 *
 * 核心读写模型:
 *   init():   上电一次性预加载所有条目到 RAM, CRC 校验后才标记 valid
 *   read():   直接返回 RAM 缓存, 零延迟, 不碰底层存储
 *   write():  立即更新 RAM 缓存, 异步入队落盘
 *   process(): 主循环调用, 从写队列取任务执行
 *
 * 驱动替换:  实现 user_eeprom_driver_t → driver_register() → 切换完成
 * 新增条目:  在 user_eeprom_items.def 中加一行
 ******************************************************************************/
#ifndef USER_EEPROM_H
#define USER_EEPROM_H

#include <stdbool.h>
#include <stdint.h>
#include "sl_status.h"
#include "user_eeprom_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

// ==========================================================================
// 标志位
// ==========================================================================

#define EEPROM_FLAG_NONE        0x00U
#define EEPROM_FLAG_PRESERVE    0x01U   /* 工厂复位时保留 */
#define EEPROM_FLAG_AUTO_CLEAR  0x02U   /* 上电自动擦除 (备用区) */

// ==========================================================================
// 条目枚举 (由 user_eeprom_items.def X-Macro 自动展开)
// ==========================================================================

#define EEPROM_ITEM_X(name, addr, dlen, label, flags) EEPROM_##name,
typedef enum {
  #include "user_eeprom_items.def"
  EEPROM_ITEM_COUNT
} user_eeprom_item_t;
#undef EEPROM_ITEM_X

// ==========================================================================
// 公共 API
// ==========================================================================

/**
 * @brief 注册存储驱动 (必须在 user_eeprom_init 前调用)
 */
void user_eeprom_driver_register(const user_eeprom_driver_t *driver);

/**
 * @brief 初始化: 预加载全部条目到 RAM 缓存
 *
 * 流程:
 *   1. 调用 driver->init()
 *   2. 遍历所有条目: driver->read() → CRC16 校验
 *   3. 校验通过 → 标记 valid + 缓存数据
 *      校验失败 → 标记 invalid (视为从未写入)
 *   4. EEPROM_FLAG_AUTO_CLEAR 条目 → driver->erase() + 标记 invalid
 */
void user_eeprom_init(void);

/**
 * @brief 主循环调用: 从异步写队列取出一个任务执行
 * @param max_time_ms  本次最多允许的阻塞毫秒数 (0=执行一个后立即返回)
 * @return true  还有待处理任务
 *         false 写队列已空
 *
 * @note  典型用法: 在 app_process_action() 中调用 user_eeprom_process(0);
 *         每次主循环执行最多一个写操作, 不阻塞 BLE 协议栈
 */
bool user_eeprom_process(uint16_t max_time_ms);

/**
 * @brief 读取条目 (零延迟, 直接返回 RAM 缓存)
 * @return SL_STATUS_OK         数据有效 (init 时 CRC 已校验通过)
 *         SL_STATUS_NOT_FOUND  从未写入或 CRC 校验失败
 */
sl_status_t user_eeprom_read(user_eeprom_item_t item, void *buf, uint16_t buf_len);

/**
 * @brief 写入条目 (立即更新 RAM + 异步落盘)
 * @param item     条目枚举
 * @param data     数据指针 (调用者可立即释放)
 * @param data_len 数据长度 (必须与定义一致)
 * @param callback 写入完成回调 (可选, NULL=不需要通知)
 *                 callback 在 process() 上下文中调用
 * @return SL_STATUS_OK  RAM 已更新, 写任务已入队
 *         SL_STATUS_INVALID_PARAMETER  参数非法或队列满降级失败
 *
 * @note  调用后 read() 立即返回新值 (来自 RAM 缓存)
 *         断电恢复: init() 时 CRC 校验失败 → 自动回退到旧 NVM 值
 */
sl_status_t user_eeprom_write(user_eeprom_item_t item,
                              const void *data, uint16_t data_len,
                              void (*callback)(user_eeprom_item_t item, sl_status_t result));

/**
 * @brief 同步写入 (阻塞直到落盘, 用于关机前关键数据)
 */
sl_status_t user_eeprom_write_sync(user_eeprom_item_t item,
                                   const void *data, uint16_t data_len);

/**
 * @brief 删除条目 (立即置无效 + 异步擦除 NVM)
 */
sl_status_t user_eeprom_delete(user_eeprom_item_t item);

/**
 * @brief 检查条目是否有效 (init 时 CRC 校验通过)
 */
bool user_eeprom_is_valid(user_eeprom_item_t item);

/**
 * @brief 获取条目的数据长度; item 非法返回 0
 */
uint16_t user_eeprom_get_data_len(user_eeprom_item_t item);

/**
 * @brief 获取条目的 NVM 地址 (调试用)
 */
uint32_t user_eeprom_get_nvm_key(user_eeprom_item_t item);

/**
 * @brief 是否有待处理的写操作 (关机前应检查)
 */
bool user_eeprom_is_write_pending(void);

/**
 * @brief 等待全部写操作完成 (阻塞, 用于关机/重启前)
 */
void user_eeprom_flush(void);

/**
 * @brief 工厂复位: 删除所有非保留条目
 * @note  保留 EEPROM_FLAG_PRESERVE 标记的条目
 */
void user_eeprom_factory_reset(void);

#ifdef __cplusplus
}
#endif

#endif /* USER_EEPROM_H */
